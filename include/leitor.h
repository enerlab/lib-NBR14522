#pragma once

#include <NBR14522.h>
#include <task_scheduler.h>

namespace NBR14522 {

template <typename T> using sptr = std::shared_ptr<T>;

class Leitor {
  public:
    typedef enum {
        RESPOSTA_RECEBIDA,
        RESPOSTA_BYTE_RECEBIDO,
        RESPOSTA_PEDACO_RECEBIDO,
        DESCONECTADO,
        SINCRONIZADO,
        ESPERANDO_RESPOSTA,
        // erros:
        NAK_TX_EXCEDIDO,
        NAK_RX_EXCEDIDO,
        TEMPO_EXCEDIDO,
        RESPOSTA_CODIGO_INVALIDO,
        SINCRONIZACAO_FALHOU,
        RESPOSTA_CODIGO_DIFERE,
        RESPOSTA_CODIGO_INVALIDO_ENQ
    } estado_t;

  private:
    estado_t _estado;
    sptr<IPorta> _porta;
    TaskScheduler<> _tasks;
    resposta_t _resposta;
    std::function<void(resposta_t& rsp)> _callbackDeResposta;
    comando_t _comando;
    size_t _respostaIndex;
    size_t _countTransmittedNAK;
    size_t _countReceivedNAK;
    bool _respostaTimedOut = false;

    void _setEstado(estado_t estado) {
        _estado = estado;

        bool stopTasks = false;

        switch (_estado) {
        case SINCRONIZADO:
            _countReceivedNAK = 0;
            _countTransmittedNAK = 0;
            _transmiteComando(_comando);
            break;
        case RESPOSTA_BYTE_RECEBIDO:
        case RESPOSTA_PEDACO_RECEBIDO:
            _tasks.addTask(std::bind(&Leitor::_readNextPieceOfResposta, this),
                           std::chrono::milliseconds(TMAXCAR_MSEC));
            break;
        case RESPOSTA_RECEBIDA:
            _callbackDeResposta(_resposta);

            if (isComposedCodeCommand(_resposta.at(0))) {
                // os unicos 3 comandos compostos existentes na norma (0x26,
                // 0x27 e 0x52) possuem o octeto 006 (5o byte), cujo valor:
                // 0N -> resposta/bloco intermediário
                // 1N -> resposta/bloco final

                if (_resposta.at(5) & 0x10) {
                    // resposta final do comando composto
                    stopTasks = true;
                } else {
                    // resposta intermediária, aguarda próxima resposta
                    _setEstado(ESPERANDO_RESPOSTA);
                }

            } else {
                // comando simples, e unica resposta foi recebida
                stopTasks = true;
            }
            break;
        case ESPERANDO_RESPOSTA:
            _tasks.addTask(std::bind(&Leitor::_waitingResposta, this));

            _respostaTimedOut = false;
            // no data within TMAXRSP or TMAXCAR? then response from meter has
            // timed out
            _tasks.addTask(std::bind(&Leitor::_waitingRespostaTimedOut, this),
                           std::chrono::milliseconds(TMAXRSP_MSEC));
            break;
        case NAK_TX_EXCEDIDO:
        case NAK_RX_EXCEDIDO:
        case TEMPO_EXCEDIDO:
        case RESPOSTA_CODIGO_INVALIDO:
        case SINCRONIZACAO_FALHOU:
        case RESPOSTA_CODIGO_DIFERE:
        case RESPOSTA_CODIGO_INVALIDO_ENQ:
            stopTasks = true;
            break;
        default:
            break;
        }

        if (stopTasks)
            _tasks.runStop();
    }
    estado_t _getEstado() { return _estado; }

    void _transmiteComando(comando_t& comando) {
        setCRC(comando, CRC16(comando.data(), comando.size() - 2));
        _porta->write(comando.data(), comando.size());
        _setEstado(ESPERANDO_RESPOSTA);
    }

    void _tentaSincronizarPeriodicamente() {
        // tentar sincronizar com medidor periodicamente até conseguir
        // sincronizar

        byte_t data;
        size_t read = _porta->read(&data, 1);
        if (read == 1 && data == ENQ) {
            // após receber o ENQ, temos até TMAXSINC_MSEC para enviar o 1o byte
            // do comando
            _setEstado(SINCRONIZADO);
        } else {
            _tasks.addTask(
                std::bind(&Leitor::_tentaSincronizarPeriodicamente, this), 5ms);
        }
    }

    void _waitingRespostaTimedOut() {
        if (_getEstado() == ESPERANDO_RESPOSTA) {
            _respostaTimedOut = true;
        }
    }

    void _waitingResposta() {

        if (_respostaTimedOut) {
            _setEstado(TEMPO_EXCEDIDO);
            return;
        }

        byte_t firstByte;
        size_t read = _porta->read(&firstByte, 1);

        if (read <= 0) {
            _tasks.addTask(std::bind(&Leitor::_waitingResposta, this), 10ms);
            return;
        }

        // recebeu byte do medidor...

        // TODO: cancel timed out task! Must be implemented in TaskScheduler
        // class.

        // check if NAK received
        if (firstByte == NAK) {
            _countReceivedNAK++;
            // devemos enviar novamente o comando ao medidor, caso menos de
            // 7 NAKs recebidos
            if (_countReceivedNAK >= MAX_BLOCO_NAK) {
                _setEstado(NAK_RX_EXCEDIDO);
            } else {
                // retransmite comando
                _transmiteComando(_comando);
            }

            return;
        }

        // check if ENQ received
        if (firstByte == ENQ) {
            _setEstado(RESPOSTA_CODIGO_INVALIDO_ENQ);
            return;
        }

        // check if codigo invalido
        if (!isValidCodeCommand(firstByte)) {
            printf("codigo invalido: %x\n", firstByte);
            _setEstado(RESPOSTA_CODIGO_INVALIDO);
            return;
        }

        // check if codigo diferente do enviado
        if (firstByte != _comando.at(0)) {
            _setEstado(RESPOSTA_CODIGO_DIFERE);
            return;
        }

        // primeiro byte recebido OK!

        _resposta.at(0) = firstByte;
        _respostaIndex = 1;
        _setEstado(RESPOSTA_BYTE_RECEBIDO);
    }

    void _readNextPieceOfResposta() {
        size_t read = _porta->read(&_resposta[_respostaIndex],
                                   _resposta.size() - _respostaIndex);

        // no data within TMAXRSP or TMAXCAR?
        if (read <= 0) {
            // response from meter has timed out
            _setEstado(TEMPO_EXCEDIDO);
            return;
        }

        _respostaIndex += read;

        if (_respostaIndex < _resposta.size()) {
            // resposta parcial recebida
            _setEstado(RESPOSTA_PEDACO_RECEBIDO);
            return;
        }

        // resposta completa recebida. Trata-a.

        uint16_t crcReceived = getCRC(_resposta);
        uint16_t crcCalculated = CRC16(_resposta.data(), _resposta.size() - 2);

        if (crcReceived != crcCalculated) {
            // se CRC com erro, o medidor deve enviar um NAK para o
            // leitor e aguardar novo COMANDO. O máximo de NAKs
            // enviados para um mesmo comando é 7.

            if (_countTransmittedNAK >= MAX_BLOCO_NAK) {
                _setEstado(NAK_TX_EXCEDIDO);
            } else {
                byte_t sinalizador = NAK;
                _porta->write(&sinalizador, 1);
                _countTransmittedNAK++;

                // aguarda retransmissao da resposta pelo medidor
                _setEstado(ESPERANDO_RESPOSTA);
            }

            return;
        }

        // resposta completa recebida está OK!

        // transmite um ACK para o medidor
        byte_t sinalizador = ACK;
        _porta->write(&sinalizador, 1);

        _setEstado(RESPOSTA_RECEBIDA);
    }

  public:
    Leitor(sptr<IPorta> porta) : _porta(porta) { _setEstado(DESCONECTADO); }

    estado_t tx(const comando_t& comando,
                std::function<void(const resposta_t& rsp)> cb_rsp_received,
                std::chrono::milliseconds timeout) {

        // discarta todos os bytes até agora da porta
        byte_t data;
        while (_porta->read(&data, 1))
            ;

        _comando = comando;
        _callbackDeResposta = cb_rsp_received;
        _setEstado(DESCONECTADO);
        _tasks.addTask(
            std::bind(&Leitor::_tentaSincronizarPeriodicamente, this));

        // timeout action
        _tasks.addTask(
            [this]() {
                if (this->_getEstado() == DESCONECTADO) {
                    this->_setEstado(SINCRONIZACAO_FALHOU);
                    this->_tasks.runStop();
                }
            },
            timeout);

        _tasks.run();

        return _getEstado();
    }
};

} // namespace NBR14522
