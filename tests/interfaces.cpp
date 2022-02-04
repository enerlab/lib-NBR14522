#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>

#include <NBR14522.h>
#include <icomando.h>
#include <ileitor.h>
#include <iporta.h>

#include <leitura_padrao.h>

using namespace NBR14522;

class PortaFake0 : public IPorta {
    PortaFake0(std::string name) { std::cout << "Iniciando PortaFake0\n"; }

    size_t available() { return 0; }

    size_t write(const byte_t* data, const size_t data_sz) { return 0; }

    size_t read(byte_t* data, const size_t max_data_sz) { return 0; }
};

Porta::getPorta(){

#ifdef WINDOWS
    return PortaWindows
#endif

}

Timer::getTimer(){};

TEST_CASE("leitor síncrono com leitura de n comandos e timeout") {

    // PortaFake0 porta0("/path/to/fakeport");
    IPorta port = Porta::getPorta();

    Leitor leitor0(porta);

    std::vector<comando_t> comandos;

    // +++ user-friendly
    comando_t cmd1 = {0x14, 0x12, 0x34, 0x56, 0x00};
    comando_t cmd2 = {0x20, 0x67, 0x00, 0x01, 0x02};
    comandos.push_back(cmd1);
    comandos.push_back(cmd2);

    std::vector<resposta_t> respostas;

    /**
     * o timeout para um comando composto vai ter que ser grande, ou melhor
     * deixar sem timeout
     *
     */
    ILeitor::status err = leitor.txrx(&comandos, &respostas, 1000);

    /**
     * a limitação neste caso é que um comando pode gerar muitas respostas
     * (comando composto) e o vetor respostas não ter espaço suficiente p/
     * armazenar tudo. Nesse caso vai dar o erro de memory out. Por outro lado a
     * API é simples de usar.
     *
     * Um meio termo é colocar o vetor respostas para ser alocado em uma região
     * de memória muito grande (estática por exemplo) que cubra o pior caso. Mas
     * isso pode ser muito custoso para um sistema limitado.
     *
     * Não tem como manter a API desse jeito e conseguir contornar essa
     * limitação. A ideia básica para contornar isso é: ir colocando as
     * respostas em um buffer que vai sendo lido e removido pelo usuário p/
     * liberar mais espaço para escrita. Combina com consumer-producer. Mas o
     * conveniente seria manter a API do meter compativel com os dois casos: API
     * simples usar e tbm API + consumer producer. Como fazer isso? Não sei.
     * Acho que o caminho mesmo é ter duas APIs separadas para cada caso, como
     * pensado inicialmente
     *
     */
}

TEST_CASE("leitor(es) assíncronos com available() + dequeue()") {

    PortaFake0 porta0("/path/to/fake/port0");
    Leitor leitor0(porta0);
    PortaFake0 porta1("path/to/fake/port1");
    Leitor leitor1(porta1);

    // loop da thread/task do leitor + usuario
    while (1) {
        leitor0.process();
        leitor1.process();

        // aqui poderia ser outro loop thread/task separada p/ leitura e
        // handling

        if (leitor0.available()) {
            comando_t cmd = leitor0.dequeue();
            // transmite/salva o comando lido
            // ...
        }

        if (leitor1.available()) {
            comando_t cmd = leitor1.dequeue();
            // transmite/salva o comando lido
            // ...
        }
    }

    // outra thread/task:

    // pega os comandos de leitura de algum evento, I/O, etc
    comando_t cmd1 = {0x14, 0x12, 0x34, 0x56, ...};
    comando_t cmd2 = {0x20, 0x67, 0x00, 0x01, 0x02, ...};
    leitor0.enqueue(cmd1);
    leitor0.enqueue(cmd2);
    leitor1.enqueue(cmd1);
    leitor1.enqueue(cmd2);
}

TEST_CASE("leitor(es) assincronos com callback") {

    /**
     * com um handler, deixamos para o usuario desta API se virar com o
     * gerenciamento dos comandos vindos. O leitor apenas recebe uma resposta e
     * passa para o usuário, assim o leitor só precisa se preocupar em ter
     * espaço para armazenar apenas uma resposta por vez.
     */
    void handler_cmd_rxed(Leitor leitor, Leitor::Status status,
                          resposta_t resposta) {
        // salva, transmite, etc a resposta recebida pelo leitor

        if (status == Leitor::Status::SUCESSO) {
            // ...
        } else {
            // ...
        }
    }

    PortaFake0 serial0("port0");
    PortaFake0 serial1("port1");

    // aqui temos que informar ao leitor de alguma forma o número máximo de
    // comando_t que ele pode manter internamente. Ou entao passar o
    // buffer/container no qual vai armazenar os comando_t. Deve ser container
    // de ponteiros ou objetos de comando_t? Acho que de ponteiros para
    // simplificar a classe Leitor, assim ele n precisa se preocupar com a
    // alocação dos comando_t, ficando essa responsabilidade pro usuário

    Leitor leitor0(serial0, handler_cmd_rxed, timer);
    Leitor leitor1(serial1, handler_cmd_rxed, timer);

    // thread/task dos leitores
    while (1) {
        leitor0.process();
        leitor1.process();
    }

    // outra thread/task:

    // pega os comandos de leitura de algum evento, I/O, etc
    comando_t cmd1 = {0x14, 0x12, 0x34, 0x56};
    comando_t cmd2 = {0x20, 0x67, 0x00, 0x01, 0x02};
    leitor0.enqueue(&cmd1);
    leitor0.enqueue(&cmd2);
    leitor1.enqueue(&cmd1);
    leitor1.enqueue(&cmd2);
}

TEST_CASE("Porta") {}

TEST_CASE("Leituras Padrão") {

    LeituraPadrao::parametros_t params = {
        .tempo_mm = 15, .tempo_mm_unidade = 0, .grupo_de_canais = 0};

    vector<comando_t> comandos =
        LeituraPadrao::getComandos(LeituraPadrao::REPOSICAO_DE_DEMANDA, params);

    // setup do leitor ...

    std::vector<resposta_t> respostas;
    ILeitor::status err = leitor.txrx(&comandos, &respostas);
}