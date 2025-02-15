#pragma once
#include <CRC.h>
#include <NBR14522.h>
#include <cstdio>
#include <functional>
#include <memory>

template <typename T> using sptr = std::shared_ptr<T>;

template <class TimerPolicy, class SerialPolicy> class LeitorFSM {
  public:
    typedef enum {
        Dessincronizado,
        Sincronizado,
        ComandoTransmitido,
        AtrasoDeSequenciaRecebido,
        CodigoRecebido,
        AguardaNovoComando,
    } estado_t;

    typedef enum {
        Sucesso,
        Processando,
        ErroLimiteDeNAKsRecebidos,
        ErroLimiteDeNAKsTransmitidos,
        ErroLimiteDeTransmissoesSemRespostas,
        ErroTempoSemWaitEsgotado,
        ErroLimiteDeWaitsRecebidos,
        ErroQuebraDeSequencia,
        ErroAposRespostaRecebeNAK,
        ErroSemRespostaAoAguardarProximaResposta,
        ExcecaoOcorrenciaNoMedidor,
        ExcecaoComandoNaoImplementado
    } status_t;

    void setComando(const NBR14522::comando_t& comando) {
        _comando = comando;
        _estado = Dessincronizado;
        _status = Processando;
        _esvaziaPortaSerial();
    }

    void
    setCallback(std::function<void(const NBR14522::resposta_t& rsp)> callback) {
        _callback = callback;
    }

    estado_t processaEstado() {

        byte_t byte;
        size_t bytesLidosSz;

        switch (_estado) {
        case AguardaNovoComando:
            // nao faz nada neste estado, aguardando comando ser setado em
            // setComando()
            break;
        case Dessincronizado:
            if (_porta->rx(&byte, 1) && byte == NBR14522::ENQ) {
                _estado = Sincronizado;
                _timer.setTimeout(NBR14522::TMAXENQ_MSEC);
            }
            break;
        case Sincronizado:
            if (_timer.timedOut()) {
                _estado = Dessincronizado;
                _esvaziaPortaSerial();
            } else if (_porta->rx(&byte, 1) && byte == NBR14522::ENQ) {
                _transmiteComando();
                _counterNakRecebido = 0;
                _counterNakTransmitido = 0;
                _counterSemResposta = 0;
                _counterWaitRecebido = 0;
                _isRespostaComposta = false;
                _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                _estado = ComandoTransmitido;
            }
            break;
        case ComandoTransmitido:
            if (_timer.timedOut()) {
                _esvaziaPortaSerial();
                _counterSemResposta++;
                if (_counterSemResposta == NBR14522::MAX_COMANDO_SEM_RESPOSTA) {
                    // falhou
                    _estado = AguardaNovoComando;
                    _status = ErroLimiteDeTransmissoesSemRespostas;
                } else if (_isRespostaComposta) {
                    // falhou
                    _estado = AguardaNovoComando;
                    _status = ErroSemRespostaAoAguardarProximaResposta;
                } else {
                    _transmiteComando();
                    _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                }
            } else if (_porta->rx(&byte, 1)) {
                // byte recebido
                if (byte == NBR14522::NAK) {
                    _counterNakRecebido++;
                    if (_counterNakRecebido == NBR14522::MAX_BLOCO_NAK) {
                        // falha
                        _status = ErroLimiteDeNAKsRecebidos;
                        _estado = AguardaNovoComando;
                    } else if (_isRespostaComposta) {
                        // falha
                        _status = ErroAposRespostaRecebeNAK;
                        _estado = AguardaNovoComando;
                    } else {
                        _transmiteComando();
                        _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                    }
                } else if (byte == NBR14522::WAIT) {
                    _estado = AtrasoDeSequenciaRecebido;
                    _timer.setTimeout(NBR14522::TSEMWAIT_SEC * 1000);
                } else if (
                    byte == _comando.at(0) ||
                    byte == NBR14522::CodigoInformacaoDeOcorrenciaNoMedidor ||
                    byte ==
                        NBR14522::CodigoInformacaoDeComandoNaoImplementado) {
                    // código do comando
                    _resposta.at(0) = byte;
                    _respostaBytesLidos = 1;
                    _timer.setTimeout(NBR14522::TMAXCAR_MSEC);
                    _estado = CodigoRecebido;
                } else if (byte == NBR14522::ENQ && _isRespostaComposta) {
                    // "se após o tempo permitido para a leitora enviar ACK
                    // este ainda não foi enviado, o medidor deve enviar ENQ
                    // aguardando o recebimento do ACK"

                    // retransmite ACK
                    byte = NBR14522::ACK;
                    _porta->tx(&byte, 1);
                } else {
                    // "a recepção de algo que que não seja SINALIZADOR ou
                    // BLOCO DE DADOS [resposta ou comando] deve provocar
                    // uma QUEBRA DE SEQUÊNCIA"
                    _estado = Dessincronizado;
                    _status = ErroQuebraDeSequencia;
                    _esvaziaPortaSerial();
                }
            }
            break;
        case AtrasoDeSequenciaRecebido:
            if (_timer.timedOut()) {
                // falhou
                _estado = AguardaNovoComando;
                _status = ErroTempoSemWaitEsgotado;
            } else if (_porta->rx(&byte, 1)) {
                // byte recebido
                if (byte == NBR14522::ENQ) {
                    _estado = ComandoTransmitido;
                    _transmiteComando();
                    _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                } else if (byte == NBR14522::WAIT) {
                    _counterWaitRecebido++;
                    if (_counterWaitRecebido == NBR14522::MAX_BLOCO_WAIT) {
                        // falhou
                        _estado = AguardaNovoComando;
                        _status = ErroLimiteDeWaitsRecebidos;
                    } else {
                        _timer.setTimeout(NBR14522::TSEMWAIT_SEC * 1000);
                    }
                } else {
                    // "a recepção de algo que que não seja SINALIZADOR ou
                    // BLOCO DE DADOS [resposta ou comando] deve provocar
                    // uma QUEBRA DE SEQUÊNCIA"
                    _estado = Dessincronizado;
                    _status = ErroQuebraDeSequencia;
                    _esvaziaPortaSerial();
                }
            }
            break;
        case CodigoRecebido:
            bytesLidosSz =
                _porta->rx(&_resposta[_respostaBytesLidos],
                           NBR14522::RESPOSTA_SZ - _respostaBytesLidos);
            _respostaBytesLidos += bytesLidosSz;

            if (bytesLidosSz)
                _timer.setTimeout(NBR14522::TMAXCAR_MSEC);

            if (_timer.timedOut()) {
                _counterSemResposta++;
                _esvaziaPortaSerial();
                if (_counterSemResposta == NBR14522::MAX_COMANDO_SEM_RESPOSTA) {
                    // falhou
                    _estado = AguardaNovoComando;
                    _status = ErroLimiteDeTransmissoesSemRespostas;
                } else {
                    _transmiteComando();
                    _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                    _estado = ComandoTransmitido;
                }
            } else if (_respostaBytesLidos >= NBR14522::RESPOSTA_SZ) {
                // resposta completa recebida, verifica CRC
                if (NBR14522::getCRC(_resposta) ==
                    CRC16(_resposta.data(), _resposta.size() - 2)) {
                    // CRC correto
                    // transmite ACK
                    byte = NBR14522::ACK;
                    _porta->tx(&byte, 1);

                    // chama callback caso tenha sido setado
                    if (_callback)
                        _callback(_resposta);

                    if (_isComposto(_resposta.at(0))) {
                        _isRespostaComposta = true;
                        if (_isUltimaRespostaDeComandoComposto(_resposta)) {
                            // resposta composta recebida por completo,
                            // sucesso
                            _estado = estado_t::AguardaNovoComando;
                            _status = Sucesso;
                        } else {
                            // resetar contadores, pois são referentes a
                            // cada resposta. Obs.: nao zera contador de NAK
                            // recebidos pois o comando já foi recebido
                            // corretamente pelo medidor e a partir de agora
                            // o medidor nao deve enviar mais NAKs.
                            _counterNakTransmitido = 0;
                            _counterSemResposta = 0;
                            _counterWaitRecebido = 0;
                            _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                            _estado = ComandoTransmitido;
                        }
                    } else {
                        // resposta simples recebida

                        if (_resposta.at(0) ==
                            NBR14522::CodigoInformacaoDeOcorrenciaNoMedidor)
                            _status = ExcecaoOcorrenciaNoMedidor;
                        else if (_resposta.at(0) ==
                                 NBR14522::
                                     CodigoInformacaoDeComandoNaoImplementado)
                            _status = ExcecaoComandoNaoImplementado;
                        else
                            _status = Sucesso;

                        _estado = estado_t::AguardaNovoComando;
                    }
                } else {
                    // CRC incorreto
                    // transmite NAK
                    byte = NBR14522::NAK;
                    _porta->tx(&byte, 1);
                    _counterNakTransmitido++;
                    if (_counterNakTransmitido == NBR14522::MAX_BLOCO_NAK) {
                        // falhou
                        _estado = estado_t::AguardaNovoComando;
                        _status = status_t::ErroLimiteDeNAKsTransmitidos;
                    } else {
                        _estado = estado_t::ComandoTransmitido;
                        _timer.setTimeout(NBR14522::TMAXRSP_MSEC);
                    }
                }
            }
            break;
        }

        return _estado;
    }

    LeitorFSM(sptr<SerialPolicy> porta) : _porta(porta) {}

    uint32_t counterNakRecebido() { return _counterNakRecebido; }
    uint32_t counterNakTransmitido() { return _counterNakTransmitido; }
    uint32_t counterSemResposta() { return _counterSemResposta; }
    uint32_t counterWaitRecebido() { return _counterWaitRecebido; }
    status_t status() { return _status; }

    NBR14522::resposta_t resposta() { return _resposta; }

  private:
    estado_t _estado = AguardaNovoComando;
    status_t _status = Processando;
    sptr<SerialPolicy> _porta;
    TimerPolicy _timer;
    NBR14522::comando_t _comando;
    NBR14522::resposta_t _resposta;
    size_t _respostaBytesLidos;
    uint32_t _counterNakRecebido = 0;
    uint32_t _counterNakTransmitido = 0;
    uint32_t _counterSemResposta = 0;
    uint32_t _counterWaitRecebido = 0;
    bool _isRespostaComposta = false;
    std::function<void(const NBR14522::resposta_t& rsp)> _callback = nullptr;

    void _esvaziaPortaSerial() {
        byte_t buf[32];
        while (_porta->rx(buf, sizeof(buf)))
            ;
    }

    void _transmiteComando() {
        // nao incluir os dois ultimos bytes de CRC no calculo do CRC
        NBR14522::setCRC(_comando, CRC16(_comando.data(), _comando.size() - 2));
        _porta->tx(_comando.data(), _comando.size());
    }

    bool _isComposto(const byte_t codigo) {
        return codigo == 0x26 || codigo == 0x27 || codigo == 0x52;
    }

    bool
    _isUltimaRespostaDeComandoComposto(const NBR14522::resposta_t& resposta) {
        return resposta.at(5) & 0x10;
    }
};
