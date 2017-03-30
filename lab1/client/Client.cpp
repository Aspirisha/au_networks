//
// Created by andy on 3/30/17.
//

#include <iostream>
#include "Client.h"
#include "../common/protocol.h"

Client::Client(const char *ip, int port) : ip(ip), port(port) { }

proto::ServerErrorCode Client::connect(const std::string &login, const std::string &password) {
    if (is_connected && login == this->login) {
        std::cerr << "already connected\n";
        return proto::CLIENT_ALREADY_CONNECTED;
    }

    sock.reset(new tcp_client_socket(ip, port));
    sock->connect();

    proto::ConnectMessage(login, password).send(*sock);
    auto response = proto::ServerMessage::receive_message(*sock);
    if (response->type() != proto::CONNECT) {
        return proto::INVALID_OPERATION;
    }

    auto connect_response = std::dynamic_pointer_cast<proto::ConnectResponse>(response);
    switch (connect_response->error) {
        case proto::SUCCESS:
            this->login = login;
            this->password = password;
            cwd = "/";
            is_connected = true;
            break;
        default:
            break;
    }
    return connect_response->error;
}

bool Client::connected() const {
    return is_connected;
}

std::string Client::get_cwd() const {
    return cwd;
}

std::string Client::get_login() const {
    return login;
}

proto::ServerErrorCode Client::ls(std::vector<std::string> &files) {
    if (!is_connected) {
        return proto::INVALID_OPERATION;
    }

    proto::LsMessage().send(*sock);
    auto response = std::dynamic_pointer_cast<proto::LsResponse>(
            proto::ServerMessage::receive_message(*sock));
    files = std::move(response->files);
    return response->error;
}











