//
// Created by andy on 3/21/17.
//

#ifndef LAB1_TCP_SOCKET_H
#define LAB1_TCP_SOCKET_H

#include "stream_socket.h"

int hostname_to_ip(const char *hostname , char* ip);
struct sockaddr;

class tcp_connection_socket : public stream_socket {
public:
    tcp_connection_socket(int socket_fd);
    ~tcp_connection_socket();

    void send(const void *buf, size_t size) override;
    void recv(void *buf, size_t size) override;

    void connect(const sockaddr *addr);
protected:
    int socket_fd;
};

class tcp_client_socket : virtual public stream_client_socket {
public:
    tcp_client_socket(const char *server_addr, int port);
    void connect() override;
    void send(const void *buf, size_t size) override;
    void recv(void *buf, size_t size) override;
protected:
    tcp_connection_socket tcp_socket;

    const char *server_addr;
    int port;
};

class tcp_server_socket : public stream_server_socket {
public:
    tcp_server_socket(const char *addr, int port);
    stream_socket* accept_one_client();
    ~tcp_server_socket();
protected:
    int socket_fd;
};

#endif //LAB1_TCP_SOCKET_H
