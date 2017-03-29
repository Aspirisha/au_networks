//
// Created by andy on 3/28/17.
//

#include <gtest/gtest.h>
#include "../common/tcp_socket.h"
#include "../server/server.h"
#include "../common/protocol.h"

namespace
{

const char msg[] = "Test message!";
const char *server_ip = "127.0.0.1";
const int server_port = 40002;

static pthread_barrier_t barrier;

void *server_func(void *void_barrier) {
    pthread_barrier_t *barrier = (pthread_barrier_t *) void_barrier;

    Server::set_root_directory("resources");
    tcp_server_socket server_socket(server_ip, server_port);

    int wait_result = pthread_barrier_wait(barrier);
    if (wait_result != 0 && wait_result != PTHREAD_BARRIER_SERIAL_THREAD) {
        throw std::logic_error("Barrier wait");
    }

    Server server(server_socket.accept_one_client());
    server.process_client_message();

    return nullptr;
}

TEST(server, connect) {
    tcp_client_socket s(server_ip, server_port);
    pthread_barrier_t barrier;

    if (pthread_barrier_init(&barrier, NULL, 2)) {
        std::cerr << "Barrier init failed\n";
        FAIL();
    }
    proto::ConnectMessage msg("vasya", "1234");
    pthread_t serv_thread;
    if (pthread_create(&serv_thread, NULL, server_func, &barrier)) {
        FAIL();
    }

    // wait until server starts listening
    int wait_result = pthread_barrier_wait(&barrier);
    if (wait_result != 0 && wait_result != PTHREAD_BARRIER_SERIAL_THREAD) {
        throw std::logic_error("Barrier wait");
    }

    s.connect();

    msg.send(s);
    auto response = proto::ServerMessage::receive_message(s);


    ASSERT_EQ(response->error, proto::SUCCESS);
    ASSERT_EQ(response->type(), proto::CONNECT);

    if (pthread_join(serv_thread, nullptr)) {
        FAIL();
    }
}
}