//
// Created by andy on 3/27/17.
//

#include <fstream>
#include <iostream>
#include "server.h"

namespace fs = boost::filesystem;

std::mutex Server::users_mutex;
std::list<UserInfo> Server::users;
boost::filesystem::path Server::root_directory;


void Server::process_client_message() {
    std::shared_ptr<proto::ClientMessage> msg = proto::ClientMessage::receive_message(client);

    switch (msg->type()) {
        case proto::CONNECT:
            process_connect(std::dynamic_pointer_cast<proto::ConnectMessage>(msg));
            break;
        case proto::DISCONNECT:
            pthread_exit(nullptr);
        case proto::CD:
            process_cd(std::dynamic_pointer_cast<proto::CdMessage>(msg));
            break;
        case proto::LS:
            process_ls(std::dynamic_pointer_cast<proto::LsMessage>(msg));
            break;
        case proto::GET:
            process_get(std::dynamic_pointer_cast<proto::GetMessage>(msg));
            break;
        case proto::PUT:
            process_put(std::dynamic_pointer_cast<proto::PutMessage>(msg));
            break;
        case proto::DEL:
            process_del(std::dynamic_pointer_cast<proto::DelMessage>(msg));
            break;
    }
}

void Server::process_connect(std::shared_ptr<proto::ConnectMessage> msg) {
    bool found = false;
    bool password_match = false;
    users_mutex.lock();
    for (auto &user: users) {
        if (user.login == msg->directory) {
            found = true;
            password_match = user.password == msg->password;
            break;
        }
    }
    if (!found) {
        users.push_back(UserInfo(msg->directory, msg->password));
    }
    users_mutex.unlock();

    if (!found) {
        proto::ConnectResponse(proto::SUCCESS).send(*client);
    } else if (!password_match) {
        proto::ConnectResponse(proto::INVALID_PASSWORD).send(*client);
        return;
    } else {
        proto::ConnectResponse(proto::SUCCESS).send(*client);
    }

    fs::create_directories(root_directory / msg->directory);
    user_root_directory = current_directory = fs::canonical(root_directory / msg->directory);
}

UserInfo::UserInfo(const std::string &login, const std::string &password) : login(login), password(password) { }


Server::Server(stream_socket * client) : client(client) { }

void Server::process_cd(std::shared_ptr<proto::CdMessage> msg) {
    fs::path dest(msg->directory);

    try {
        dest = fs::canonical(dest, current_directory);
    } catch (fs::filesystem_error &e) {
        proto::CdResponse(proto::INVALID_OPERATION, current_directory.string()).send(*client);
        return;
    }

    const std::string &user_root = user_root_directory.string();
    if (dest.string().compare(0, user_root.size(), user_root)) {
        proto::CdResponse(proto::INVALID_OPERATION, current_directory.string()).send(*client);
        return;
    }

    current_directory = dest;
    proto::CdResponse(proto::SUCCESS, dest.string()).send(*client);
}

void Server::process_get(std::shared_ptr<proto::GetMessage> msg) {
    if (!fs::exists(msg->src_file)) {
        proto::GetResponse(proto::INVALID_OPERATION, {}).send(*client);
        return;
    }

    std::ifstream in(msg->src_file, std::ifstream::binary);
    uintmax_t size = fs::file_size(msg->src_file);
    std::vector<uint8_t> data(size);
    in.read((char *) data.data(), size);

    proto::GetResponse(proto::SUCCESS, data).send(*client);
}

void Server::process_put(std::shared_ptr<proto::PutMessage> msg) {
    std::ofstream out(msg->dst_file, std::ofstream::binary);
    out.write((const char *) msg->file_data.data(), msg->file_data.size());
    proto::PutResponse(proto::SUCCESS).send(*client);
}

void Server::process_ls(std::shared_ptr<proto::LsMessage> msg) {
    std::vector<std::string> entries;
    for(auto entry = fs::directory_iterator(current_directory); entry != fs::directory_iterator(); ++entry) {
        entries.push_back(entry->path().string());
    }

    proto::LsResponse(proto::SUCCESS, entries).send(*client);
}

void Server::process_del(std::shared_ptr<proto::DelMessage> msg) {
    fs::remove(msg->filename);
    proto::DelResponse(proto::SUCCESS).send(*client);
}

void Server::set_root_directory(const fs::path &root_dir) {
    if (!fs::exists(root_dir)) {
        fs::create_directories(root_dir);
    }
    root_directory = root_dir;
}

Server::~Server() {
    delete client;
}

















