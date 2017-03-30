//
// Created by andy on 3/26/17.
//

#ifndef LAB1_SERVER_H
#define LAB1_SERVER_H

#include <list>
#include <mutex>
#include <utility>
#include "protocol.h"
#include <boost/filesystem.hpp>
#include <atomic>
#include "tcp_socket.h"

struct UserInfo {
    UserInfo(const std::string &login, const std::string &password, bool is_connected = false);

    std::string login;
    std::string password;
    bool is_connected;
};

/**
 * Each server processes single Client
 */
class Server {
public:
    Server(stream_socket *client);
    ~Server();
    void process_client_message();

    static void set_root_directory(const boost::filesystem::path &root_dir);
    static void read_users_info();
    static void save_clients_info();

private:
    void process_connect(std::shared_ptr<proto::ConnectMessage> msg);
    //void process_disconnect(std::shared_ptr<proto::Dis> msg);
    void process_cd(std::shared_ptr<proto::CdMessage> msg);
    void process_get(std::shared_ptr<proto::GetMessage> msg);
    void process_put(std::shared_ptr<proto::PutMessage> msg);
    void process_del(std::shared_ptr<proto::DelMessage> msg);
    void process_ls(std::shared_ptr<proto::LsMessage> msg);
    void process_pwd(std::shared_ptr<proto::PwdMessage> msg);

    static std::mutex users_mutex;
    static std::list<UserInfo> users;
    static boost::filesystem::path root_directory;
    static boost::filesystem::path users_info_file;
    static const int max_login_length;
    static const int max_password_length;


    stream_socket *client;
    std::string login;
    boost::filesystem::path current_directory;
    boost::filesystem::path user_root_directory;
    bool is_connected = false;
};

#endif //LAB1_SERVER_H
