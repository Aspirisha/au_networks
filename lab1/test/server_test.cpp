//
// Created by andy on 3/28/17.
//

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <fstream>
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

    void connect(const UserInfo &user, proto::ServerErrorCode expected_error = proto::SUCCESS) {
        proto::ConnectMessage msg(user.login, user.password);
        auto response = send_message_and_get_response(msg);
        ASSERT_EQ(response->error, expected_error);
        ASSERT_EQ(response->type(), proto::CONNECT);
    }

    void pwd(const std::string &expected_cwd) {
        proto::PwdMessage pwd_message;
        auto response = send_message_and_get_response(pwd_message);
        ASSERT_EQ(response->error, proto::SUCCESS);
        ASSERT_EQ(response->type(), proto::PWD);

        auto pwd_response = std::dynamic_pointer_cast<proto::PwdResponse>(response);
        ASSERT_EQ(pwd_response->cwd, expected_cwd);
    }

    void ls(const std::set<std::string> &expected_files) {
        proto::LsMessage ls_message;
        auto response = send_message_and_get_response(ls_message);
        ASSERT_EQ(response->error, proto::SUCCESS);
        ASSERT_EQ(response->type(), proto::LS);

        auto ls_response = std::dynamic_pointer_cast<proto::LsResponse>(response);

        std::set<std::string> response_files(ls_response->files.begin(), ls_response->files.end());
        ASSERT_EQ(expected_files, response_files);
    }

    void get(const std::string server_file, const std::string &reference_file, proto::ServerErrorCode expected_error = proto::SUCCESS) {
        proto::GetMessage get_message(server_file);
        auto response = send_message_and_get_response(get_message);
        ASSERT_EQ(response->error, expected_error);
        ASSERT_EQ(response->type(), proto::GET);

        std::ifstream in(reference_file, std::ifstream::binary);

        // this of course should be really read partially and sent chunk by chunk,
        // but let's keep everything simple for now
        uintmax_t size = fs::file_size(reference_file);
        std::vector<uint8_t> data(size);
        in.read((char *) data.data(), size);

        if (expected_error == proto::SUCCESS) {
            auto get_response = std::dynamic_pointer_cast<proto::GetResponse>(response);
            ASSERT_EQ(data.size(), get_response->file_data.size());
            ASSERT_EQ(0, memcmp(data.data(), get_response->file_data.data(), size));
        }
    }


    void put(const std::string server_file, const std::string &reference_file, proto::ServerErrorCode expected_error = proto::SUCCESS) {
        std::ifstream in(reference_file, std::ifstream::binary);

        // this of course should be really read partially and sent chunk by chunk,
        // but let's keep everything simple for now
        uintmax_t size = fs::file_size(reference_file);
        std::vector<uint8_t> data(size);
        in.read((char *) data.data(), size);

        proto::PutMessage put_message(server_file, data);
        auto response = send_message_and_get_response(put_message);
        ASSERT_EQ(response->error, expected_error);
        ASSERT_EQ(response->type(), proto::PUT);
    }

    void cd(const std::string& dir, proto::ServerErrorCode expected_error = proto::SUCCESS) {
        proto::CdMessage cd_message(dir);
        auto response = send_message_and_get_response(cd_message);
        ASSERT_EQ(response->error, expected_error);
        ASSERT_EQ(response->type(), proto::CD);
    }

    void TearDown() override {
        if (messages_sent == info.messages_number) {
            if (pthread_join(serv_thread, nullptr)) {
                FAIL();
            }
        } else {
            pthread_cancel(serv_thread);
            FAIL();
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
    connect(Vasya);
}

TEST_F(SingleClientServerInstance, connect_double) {
    SetUp(2);
    connect(Vasya);
    connect(Vasya, proto::CLIENT_ALREADY_CONNECTED);
}

TEST_F(SingleClientServerInstance, connect_invalid_names) {
    std::vector<std::string> invalid_names = {"va sya", "111vasya", ""};
    SetUp(invalid_names.size());

    for (const std::string &name: invalid_names) {
        connect({name, "1234"}, proto::INVALID_LOGIN);
    }
}

TEST_F(SingleClientServerInstance, connect_invalid_passwords) {
    std::vector<std::string> invalid_passwords = {"va sya", "", "veryveryverylongpasswordsarebad"};
    SetUp(invalid_passwords.size());

    for (const std::string &p: invalid_passwords) {
        connect({"vasya", p}, proto::INVALID_PASSWORD);
    }
}

TEST_F(SingleClientServerInstance, ls) {
    SetUp(2);
    connect(Petya);
    ls({"file.txt", "folder1", "folder2"});
}

TEST_F(SingleClientServerInstance, pwd) {
    SetUp(2);
    connect(Petya);
    pwd("/");
}

TEST_F(SingleClientServerInstance, cd_pwd) {
    SetUp(3);
    connect(Petya);
    cd("folder2");
    pwd("/folder2");
}

TEST_F(SingleClientServerInstance, cd_identity) {
    SetUp(3);
    connect(Petya);
    cd(".");
    pwd("/");
}

TEST_F(SingleClientServerInstance, get) {
    SetUp(2);
    connect(Petya);
    get("file.txt", "resources/petya/file.txt");
}

TEST_F(SingleClientServerInstance, put) {
    SetUp(5);
    connect(Petya);
    put("file_put.txt", "resources/petya/file.txt");
    get("file_put.txt", "resources/petya/file.txt");
    put("folder1/file_put.txt", "resources/petya/file.txt");
    get("folder1/file_put.txt", "resources/petya/file.txt");
}

TEST_F(SingleClientServerInstance, get_fail) {
    SetUp(2);
    connect(Petya);
    get("unexistent.txt", "resources/petya/file.txt", proto::FILE_NOT_FOUND);
}

TEST_F(SingleClientServerInstance, put_fail) {
    SetUp(2);
    connect(Petya);
    put("unexistent/file.txt", "resources/petya/file.txt", proto::INVALID_OPERATION);
}


}