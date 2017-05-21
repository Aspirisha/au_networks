//
// Created by andy on 3/30/17.
//

#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <cstring>
#include <au_socket.h>
#include "Client.h"
#include "../easyloggingpp/src/easylogging++.h"

Client::Client(const char *ip, int port) : ip(ip), port(port) { }

proto::ServerErrorCode Client::connect(const std::string &login, const std::string &password) {
    if (is_connected && login == this->login) {
        return proto::CLIENT_ALREADY_CONNECTED;
    }

    const char *sock_type = getenv("STREAM_SOCK_TYPE");
    if (!sock_type || !strcmp(sock_type, "tcp")) {
        sock.reset(new tcp_client_socket(ip, port));
        LOG(DEBUG) << "Using tcp_stream_socket implementation";
    } else if (!strcmp(sock_type, "au")) {
        LOG(DEBUG) << "Using au_stream_socket implementation";
        sock.reset(new au_stream_client_socket(ip, port));
    } else {
        return proto::UNKNOWN_SOCKET;
    }

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

proto::ServerErrorCode Client::cd(const std::string &dir) {
    if (!is_connected) {
        return proto::INVALID_OPERATION;
    }

    proto::CdMessage(dir).send(*sock);
    auto response = std::dynamic_pointer_cast<proto::CdResponse>(
            proto::ServerMessage::receive_message(*sock));
    cwd = response->new_directory;

    return response->error;
}

proto::ServerErrorCode Client::put(const std::string &file, const std::string &localfile) {
    if (!is_connected) {
        return proto::INVALID_OPERATION;
    }


    std::ifstream in(localfile, std::ifstream::binary);

    // this of course should be really read partially and sent chunk by chunk,
    // but let's keep everything simple for now
    struct stat stat_buf;
    int rc = stat(localfile.c_str(), &stat_buf);
    int size = rc == 0 ? stat_buf.st_size : -1;
    if (size == -1)
        return proto::INVALID_OPERATION;

    std::vector<uint8_t> data(size);
    in.read((char *) data.data(), size);

    proto::PutMessage(file, data).send(*sock);
    auto response = std::dynamic_pointer_cast<proto::PutResponse>(
            proto::ServerMessage::receive_message(*sock));

    return response->error;
}

const char *Client::get_ip() const {
    return ip;
}

// currently works only with saving file to existing
proto::ServerErrorCode Client::get(const std::string &file,
                                   const std::string &localfile) {
    if (!is_connected) {
        return proto::INVALID_OPERATION;
    }

    proto::GetMessage(file).send(*sock);
    auto response = std::dynamic_pointer_cast<proto::GetResponse>(
            proto::ServerMessage::receive_message(*sock));
    std::ofstream out(localfile, std::ifstream::binary);

    if (response->error == proto::SUCCESS) {
        out.write((const char *) response->file_data.data(),
                  response->file_data.size());
    }
    return response->error;
}

proto::ServerErrorCode Client::del(const std::string &file) {
    if (!is_connected) {
        return proto::INVALID_OPERATION;
    }

    proto::DelMessage(file).send(*sock);

    auto response = std::dynamic_pointer_cast<proto::DelResponse>(
            proto::ServerMessage::receive_message(*sock));
    return response->error;
}

proto::ServerErrorCode Client::pwd(std::string &s) {
    if (!is_connected) {
        return proto::INVALID_OPERATION;
    }

    s = cwd;

    return proto::SUCCESS;
}























