//
// Created by andy on 3/27/17.
//

#include <fstream>
#include <iostream>
#include <regex>
#include "server.h"
#include "util.h"
#include "../easyloggingpp/src/easylogging++.h"

namespace fs = boost::filesystem;

std::mutex Server::users_mutex;
std::list<UserInfo> Server::users;
boost::filesystem::path Server::root_directory;
boost::filesystem::path Server::users_info_file;
const int Server::max_login_length = 30;
const int Server::max_password_length = 15;

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
        case proto::PWD:
            process_pwd(std::dynamic_pointer_cast<proto::PwdMessage>(msg));
            break;
    }
}

void Server::process_connect(std::shared_ptr<proto::ConnectMessage> msg) {
    if (is_connected) {
        proto::ConnectResponse(proto::CLIENT_ALREADY_CONNECTED).send(*client);
    }

    bool found = false;
    bool password_match = false;
    std::regex name_regex("[a-z]([a-z0-9]*)");
    if (!std::regex_match(msg->login, name_regex) || msg->login.size() > max_login_length) {
        proto::ConnectResponse(proto::INVALID_LOGIN).send(*client);
        return;
    }

    std::regex password_regex("([a-z0-9]+)");
    if (!std::regex_match(msg->password, password_regex) || msg->password.size() > max_password_length) {
        proto::ConnectResponse(proto::INVALID_PASSWORD).send(*client);
        return;
    }

    users_mutex.lock();
    for (auto &user: users) {
        if (user.login == msg->login) {
            if (user.is_connected) {
                try {
                    proto::ConnectResponse(
                            proto::CLIENT_ALREADY_CONNECTED).send(*client);
                } catch (std::exception & e) {
                    std::cerr << e.what() << std::endl;
                }
                users_mutex.unlock();
                return;
            } else {
                user.is_connected = true;
            }
            found = true;
            password_match = user.password == msg->password;
            break;
        }
    }
    if (!found) {
        users.push_back(UserInfo(msg->login, msg->password, true));
    }
    users_mutex.unlock();

    if (!found) {
        proto::ConnectResponse(proto::SUCCESS).send(*client);
    } else if (!password_match) {
        proto::ConnectResponse(proto::WRONG_PASSWORD).send(*client);
        return;
    } else {
        proto::ConnectResponse(proto::SUCCESS).send(*client);
    }

    std::cout << "Client " << msg->login << " connected\n";
    fs::create_directories(root_directory / msg->login);
    login = msg->login;
    user_root_directory = current_directory = fs::canonical(root_directory / msg->login);
    is_connected = true;
}

UserInfo::UserInfo(const std::string &login, const std::string &password, bool is_connected)
        : login(login), password(password), is_connected(is_connected) { }


Server::Server(stream_socket * client) : client(client) { }

void Server::process_cd(std::shared_ptr<proto::CdMessage> msg) {
    fs::path dest(msg->directory);

    try {
        dest = fs::canonical(dest, current_directory);
    } catch (fs::filesystem_error &e) {
        dest = "/"/relative_to(user_root_directory, current_directory);
        proto::CdResponse(proto::INVALID_OPERATION, dest.string()).send(*client);
        return;
    }

    const std::string &user_root = user_root_directory.string();
    if (dest.string().compare(0, user_root.size(), user_root)) {
        dest = "/"/relative_to(user_root_directory, current_directory);
        proto::CdResponse(proto::INVALID_OPERATION, dest.string()).send(*client);
        return;
    }

    current_directory = dest;
    std::cout << "client " << login << " changes directory to " << dest.string() << "\n";
    dest = "/"/relative_to(user_root_directory, dest);
    proto::CdResponse(proto::SUCCESS, dest.string()).send(*client);
}

void Server::process_get(std::shared_ptr<proto::GetMessage> msg) {
    fs::path realpath = current_directory/msg->src_file;

    if (!fs::exists(realpath)) {
        LOG(INFO) << "client " << login << " tries to get unexistent file " << realpath.string();
        proto::GetResponse(proto::FILE_NOT_FOUND, {}).send(*client);
        return;
    }

    std::ifstream in(realpath.string(), std::ifstream::binary);

    // this of course should be really read partially and sent chunk by chunk,
    // but let's keep everything simple for now
    uintmax_t size = fs::file_size(realpath);
    std::vector<uint8_t> data(size);
    in.read((char *) data.data(), size);
    std::cout << "client " << login << " gets file " << realpath.string() << "\n";
    proto::GetResponse(proto::SUCCESS, data).send(*client);
}

void Server::process_put(std::shared_ptr<proto::PutMessage> msg) {
    fs::path realpath = current_directory/msg->dst_file;

    fs::path dir = realpath.parent_path();
    if (!fs::exists(dir)) {
        try {
            fs::create_directories(dir);
        } catch (fs::filesystem_error &e) {
            proto::PutResponse(proto::INVALID_OPERATION).send(*client);
        }
    }

    std::ofstream out(realpath.string(), std::ofstream::binary);
    out.write((const char *) msg->file_data.data(), msg->file_data.size());

    if (out.tellp() != msg->file_data.size()) {
        proto::PutResponse(proto::INVALID_OPERATION).send(*client);
    } else {
        LOG(INFO) << "client " << login << " puts file " << realpath.string();
        proto::PutResponse(proto::SUCCESS).send(*client);
    }
}

void Server::process_ls(std::shared_ptr<proto::LsMessage> msg) {
    std::vector<std::string> entries;
    LOG(INFO) << "list directory content for " << login;
    for(auto entry = fs::directory_iterator(current_directory); entry != fs::directory_iterator(); ++entry) {
        entries.push_back(entry->path().filename().string());
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
    users_info_file = root_directory / "users.txt";
    read_users_info();
}

Server::~Server() {
    users_mutex.lock();
    for (auto &user : users) {
        if (user.login == login) {
            user.is_connected = false;
            break;
        }
    }
    users_mutex.unlock();

    delete client;
}

void Server::save_clients_info() {
    users_mutex.lock();
    std::ofstream out(users_info_file.string());
    for (UserInfo &user: users) {
        out << user.login << " " << user.password << std::endl;
    }
    users_mutex.unlock();
}

void Server::read_users_info() {
    std::ifstream in(users_info_file.string());
    users_mutex.lock();
    users.clear();

    std::string login, password;
    while (in >> login >> password) {
        users.push_back({login, password});
        fs::create_directories(root_directory / login);
    }
    users_mutex.unlock();
}

void Server::process_pwd(std::shared_ptr<proto::PwdMessage> msg) {
    fs::path rel = relative_to(user_root_directory, current_directory);
    auto abs = "/" / rel;

    proto::PwdResponse(proto::SUCCESS, abs.string()).send(*client);
}























