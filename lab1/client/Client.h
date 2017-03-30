//
// Created by andy on 3/30/17.
//

#ifndef LAB1_CLIENT_H
#define LAB1_CLIENT_H

#include <string>
#include <bits/unique_ptr.h>
#include "../common/tcp_socket.h"
#include "../common/protocol.h"

class Client {
public:
    Client(const char* ip, int port);
    proto::ServerErrorCode connect(const std::string &login, const std::string &password);
    proto::ServerErrorCode ls(std::vector<std::string> &files);

    bool connected() const;

    std::string get_cwd() const;
    std::string get_login() const;
private:
    std::unique_ptr<tcp_client_socket> sock;

    const char *ip;
    int port;

    std::string login;
    std::string password;
    std::string cwd;

    bool is_connected = false;
};


#endif //LAB1_CLIENT_H
