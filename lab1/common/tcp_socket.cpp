//
// Created by andy on 3/21/17.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include "tcp_socket.h"

tcp_connection_socket::tcp_connection_socket(int socket_fd) : socket_fd(socket_fd) {
    if (-1 == socket_fd) {
        throw std::logic_error("Invalid socket fd");
    }
}

tcp_connection_socket::~tcp_connection_socket() {
    close(socket_fd);
}

void tcp_connection_socket::recv(void *buf, size_t size) {
    size_t read_bytes = 0;

    char *cbuf = (char *) buf;
    while (read_bytes < size) {
        ssize_t rb = read(socket_fd, cbuf + read_bytes, size - read_bytes);
        if (rb <= 0) {
            throw std::logic_error("Error reading");
        }

        read_bytes += rb;
    }
}

void tcp_connection_socket::send(const void *buf, size_t size) {
    size_t written_bytes = 0;

    char *cbuf = (char *) buf;
    while (written_bytes < size) {
        ssize_t wb = write(socket_fd, cbuf + written_bytes, size - written_bytes);
        if (wb <= 0) {
            throw std::logic_error("Error writing");
        }

        written_bytes += wb;
    }
}

void tcp_connection_socket::connect(const sockaddr *addr) {
    ::connect(socket_fd, addr, sizeof(struct sockaddr));
}


int hostname_to_ip(const char *hostname, char *ip) {
    struct hostent *he;
    struct in_addr **addr_list;
    int i;

    if ( (he = gethostbyname( hostname ) ) == NULL) {
        // get the host info
        herror("gethostbyname");
        return 1;
    }

    addr_list = (struct in_addr **) he->h_addr_list;

    for(i = 0; addr_list[i] != NULL; i++) {
        //Return the first one;
        strcpy(ip , inet_ntoa(*addr_list[i]) );
        return 0;
    }

    return 1;
}


tcp_client_socket::tcp_client_socket(const char *server_addr, int port) :
        tcp_socket(socket(AF_INET, SOCK_STREAM, 0)),
        server_addr(server_addr), port(port) { }

sockaddr_in init_address(const char *server_addr, int port) {
    sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_addr, &serv_addr.sin_addr) <= 0) {
        char ip[30];
        if (hostname_to_ip(server_addr, ip)) {
            throw std::logic_error("Couldn't connect");
        }

        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
            throw std::logic_error("Couldn't connect");
        }
    }
    return serv_addr;
}

void tcp_client_socket::connect() {
    sockaddr_in serv_addr = init_address(server_addr, port);
    tcp_socket.connect((const sockaddr *) &serv_addr);
}

void tcp_client_socket::send(const void *buf, size_t size) {
    tcp_socket.send(buf, size);
}

void tcp_client_socket::recv(void *buf, size_t size) {
    tcp_socket.recv(buf, size);
}


tcp_server_socket::tcp_server_socket(const char *addr, int port) {
    protoent * protocol = getprotobyname("TCP");
    socket_fd = socket(AF_INET, SOCK_STREAM, protocol->p_proto);

    if (-1 == socket_fd) {
        throw std::logic_error("Error creating socket");
    }

    sockaddr_in serv_addr = init_address(addr, port);

    if (bind(socket_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        throw std::logic_error("Error binding server socket");
    }

    listen(socket_fd, 5);
}

tcp_server_socket::~tcp_server_socket() {
    close(socket_fd);
}

stream_socket *tcp_server_socket::accept_one_client() {
    sockaddr_in client_addr;
    socklen_t clilen = sizeof(client_addr);
    int newsockfd = accept(socket_fd, (struct sockaddr *) &client_addr, &clilen);

    return new tcp_connection_socket(newsockfd);
}





