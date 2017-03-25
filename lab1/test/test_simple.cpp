//
// Created by andy on 3/25/17.
//

#include <gtest/gtest.h>
#include <atomic>
#include "../common/tcp_socket.h"



namespace
{

const char msg[] = "Test message!";

void *send_message(void *void_socket) {
    tcp_server_socket *socket = (tcp_server_socket *) void_socket;
    stream_socket *client = socket->accept_one_client();

    client->send(msg, strlen(msg) + 1);

    return nullptr;
}

TEST(simple, clientsocket) {
    tcp_server_socket sock1("127.0.0.1", 1234);
    tcp_client_socket sock2("127.0.0.1", 1234);

    pthread_t thread1;

    if (pthread_create(&thread1, NULL, send_message, &sock1)) {
        FAIL();
    }

    char data[30] = {0};
    sock2.connect();
    sock2.recv(data, strlen(msg) + 1);
    if (pthread_join(thread1, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        FAIL();
    }

    ASSERT_TRUE(!strcmp(msg, data));
}
}