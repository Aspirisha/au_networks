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
#include "tcp_socket.h"

struct UserInfo {
    UserInfo(const std::string &login, const std::string &password);

    std::string login;
    std::string password;
};

/**
 * Each server processes single client
 */
class Server {
public:
    Server(stream_socket *client);
    ~Server();
    void process_client_message();

    static void set_root_directory(const boost::filesystem::path &root_dir);
private:
    void process_connect(std::shared_ptr<proto::ConnectMessage> msg);
    //void process_disconnect(std::shared_ptr<proto::Dis> msg);
    void process_cd(std::shared_ptr<proto::CdMessage> msg);
    void process_get(std::shared_ptr<proto::GetMessage> msg);
    void process_put(std::shared_ptr<proto::PutMessage> msg);
    void process_del(std::shared_ptr<proto::DelMessage> msg);
    void process_ls(std::shared_ptr<proto::LsMessage> msg);

    static std::mutex users_mutex;
    static std::list<UserInfo> users;
    static boost::filesystem::path root_directory;

    stream_socket *client;
    boost::filesystem::path current_directory;
    boost::filesystem::path user_root_directory;
};

#endif //LAB1_SERVER_H
