//
// Created by andy on 3/30/17.
//

#ifndef LAB1_CLIENT_H
#define LAB1_CLIENT_H

#include <string>
#include <memory>
#include "tcp_socket.h"
#include "protocol.h"

class Client {
public:
    Client(const char* ip, int port);
    proto::ServerErrorCode connect(const std::string &login, const std::string &password);
    proto::ServerErrorCode ls(std::vector<std::string> &files);
    proto::ServerErrorCode cd(const std::string &dir);
    proto::ServerErrorCode put(const std::string &file, const std::string &localfile);
    proto::ServerErrorCode get(const std::string &file, const std::string &localfile);
    proto::ServerErrorCode del(const std::string &file);
    proto::ServerErrorCode pwd(std::string &s);

    bool connected() const;

    std::string get_cwd() const;
    std::string get_login() const;
    const char *get_ip() const;
private:
    std::unique_ptr<stream_client_socket> sock;

    const char *ip;
    int port;

    std::string login;
    std::string password;
    std::string cwd;

    bool is_connected = false;
};


#endif //LAB1_CLIENT_H
