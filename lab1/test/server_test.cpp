//
// Created by andy on 3/28/17.
//

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "../common/tcp_socket.h"
#include "../server/server.h"
#include "../common/protocol.h"

namespace
{

namespace fs = boost::filesystem;

const char msg[] = "Test message!";
const char *server_ip = "127.0.0.1";
const char *test_root = "testroot";
const char *root_layout = "resources";

const UserInfo Vasya("vasya", "1234");
const UserInfo Petya("petya", "1111");


const int server_port = 40002;

struct TestInfo {
    pthread_barrier_t barrier;
    int messages_number;
};

void copyDirectoryRecursively(const fs::path& sourceDir, const fs::path& destinationDir)
{
    if (!fs::exists(sourceDir) || !fs::is_directory(sourceDir))
    {
        throw std::runtime_error("Source login " + sourceDir.string() + " does not exist or is not a login");
    }
    if (fs::exists(destinationDir))
    {
        throw std::runtime_error("Destination login " + destinationDir.string() + " already exists");
    }
    if (!fs::create_directory(destinationDir))
    {
        throw std::runtime_error("Cannot create destination login " + destinationDir.string());
    }

    for (const auto& dirEnt : fs::recursive_directory_iterator{sourceDir})
    {
        const auto& path = dirEnt.path();
        auto relativePathStr = path.string();
        boost::replace_first(relativePathStr, sourceDir.string(), "");
        fs::copy(path, destinationDir / relativePathStr);
    }
}

void *server_func(void *void_barrier) {
    TestInfo *info = (TestInfo *) void_barrier;
    tcp_server_socket server_socket(server_ip, server_port);

    int wait_result = pthread_barrier_wait(&info->barrier);
    if (wait_result != 0 && wait_result != PTHREAD_BARRIER_SERIAL_THREAD) {
        throw std::logic_error("Barrier wait");
    }
    Server server(server_socket.accept_one_client());

    for (int i = 0; i < info->messages_number; i++) {
        server.process_client_message();
    }
    return nullptr;
}

class SingleClientServerInstance : public ::testing::Test {
protected:
    void SetUp(int messages_number) {
        fs::remove_all(test_root);
        copyDirectoryRecursively(root_layout, test_root);

        Server::set_root_directory(test_root);
        client = new tcp_client_socket(server_ip, server_port);

        info.messages_number = messages_number;

        if (pthread_barrier_init(&info.barrier, NULL, 2)) {
            std::cerr << "Barrier init failed\n";
            FAIL();
        }

        if (pthread_create(&serv_thread, NULL, server_func, &info)) {
            FAIL();
        }

        // wait until server starts listening
        int wait_result = pthread_barrier_wait(&info.barrier);
        if (wait_result != 0 && wait_result != PTHREAD_BARRIER_SERIAL_THREAD) {
            throw std::logic_error("Barrier wait");
        }

        client->connect();

    }
    void TearDown() override {
        if (messages_sent == info.messages_number) {
            if (pthread_join(serv_thread, nullptr)) {
                FAIL();
            }
        } else {
            pthread_cancel(serv_thread);
        }
        delete client;
    }

    std::shared_ptr<proto::ServerMessage> send_message_and_get_response(proto::ClientMessage &msg) {
        msg.send(*client);
        auto result = proto::ServerMessage::receive_message(*client);
        messages_sent++;
        return result;
    }

    pthread_t serv_thread;
    tcp_client_socket *client = nullptr;
    TestInfo info;
    int messages_sent = 0;
};

TEST_F(SingleClientServerInstance, connect_simple) {
    SetUp(1);
    proto::ConnectMessage msg(Vasya.login, Vasya.password);

    auto response = send_message_and_get_response(msg);
    ASSERT_EQ(response->error, proto::SUCCESS);
    ASSERT_EQ(response->type(), proto::CONNECT);
}

TEST_F(SingleClientServerInstance, connect_double) {
    SetUp(2);
    proto::ConnectMessage msg(Vasya.login, Vasya.password);

    auto response = send_message_and_get_response(msg);
    ASSERT_EQ(response->error, proto::SUCCESS);
    ASSERT_EQ(response->type(), proto::CONNECT);

    response = send_message_and_get_response(msg);
    ASSERT_EQ(response->error, proto::CLIENT_ALREADY_CONNECTED);
    ASSERT_EQ(response->type(), proto::CONNECT);
}

TEST_F(SingleClientServerInstance, connect_invalid_names) {
    std::vector<std::string> invalid_names = {"va sya", "111vasya", ""};
    SetUp(invalid_names.size());

    for (const std::string &name: invalid_names) {
        proto::ConnectMessage msg(name, "1234");
        auto response = send_message_and_get_response(msg);
        ASSERT_EQ(response->error, proto::INVALID_LOGIN);
        ASSERT_EQ(response->type(), proto::CONNECT);
    }
}

TEST_F(SingleClientServerInstance, connect_invalid_passwords) {
    std::vector<std::string> invalid_passwords = {"va sya", "", "veryveryverylongpasswordsarebad"};
    SetUp(invalid_passwords.size());

    for (const std::string &p: invalid_passwords) {
        proto::ConnectMessage msg("vasya", p);
        auto response = send_message_and_get_response(msg);
        ASSERT_EQ(response->error, proto::INVALID_PASSWORD);
        ASSERT_EQ(response->type(), proto::CONNECT);
    }
}

TEST_F(SingleClientServerInstance, connect_wrong_passwords) {
    std::vector<std::string> invalid_passwords = {"va sya", "", "veryveryverylongpasswordsarebad"};
    SetUp(invalid_passwords.size());

    for (const std::string &p: invalid_passwords) {
        proto::ConnectMessage msg("vasya", p);
        auto response = send_message_and_get_response(msg);
        ASSERT_EQ(response->error, proto::INVALID_PASSWORD);
        ASSERT_EQ(response->type(), proto::CONNECT);
    }
}

}