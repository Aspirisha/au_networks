//
// Created by andy on 3/21/17.
//

#include <iostream>
#include <memory>
#include "tcp_socket.h"
#include "protocol.h"
#include "Client.h"
#include "../easyloggingpp/src/easylogging++.h"

void process_cd(Client &client);

using namespace std;

namespace {
const int default_port = 40001;
const char *default_ip = "127.0.0.1";
}

void print_help() {
    cout << "Available commands: \n";
    cout << "\thelp -- print this help\n";
    cout << "\texit -- exit form the program\n";
    cout << "\tconnect <login> <password> -- connect to the server vfs with given credentials\n";
    cout << "\tpwd -- print current vfs directory\n";
    cout << "\tls -- print current vfs directory content\n";
    cout << "\tcd <path> -- change current vfs directory\n";
    cout << "\tdel <path> -- delete vfs file or directory\n";
    cout << "\tget <filename> <local file> -- download file from server and save it to <local filename>\n";
    cout << "\tput <local filename> <filename> -- upload <local filename> to server\n";
}

void process_connect(Client &client) {
    string password;
    string login;
    cin >> login >> password;

    try {
        switch (client.connect(login, password)) {
            case proto::SUCCESS:
                cout << "Connected successfully\n";
                break;
            case proto::CLIENT_ALREADY_CONNECTED:
                std::cerr << "already connected\n";
                break;
            case proto::WRONG_PASSWORD:
                std::cerr << "wrong password\n";
                break;
            case proto::UNKNOWN_SOCKET:
                std::cerr << "Environment variable $STREAM_SOCK_TYPE defines unknown socket type: "
                    << getenv("STREAM_SOCK_TYPE") << std::endl;
                break;
            default:
                break;
        }
    } catch (std::logic_error &e) {
        LOG(DEBUG) << e.what();
        LOG(ERROR) << "Error connecting to server";
    }
}

void process_ls(Client &client) {
    vector<string> files;
    switch (client.ls(files)) {
        case proto::INVALID_OPERATION:
            std::cerr << "not connected\n";
            break;
        case proto::SUCCESS:
            for (const string &s: files) {
                cout << s << endl;
            }
            break;
        default:
            std::cerr << "server error\n";
            break;
    }
}

struct GetPutData {
    GetPutData(Client &client) : client(client) {}

    string file;
    string local_file;
    Client &client;
};

void print_greeting(Client &client) {
    if (!client.connected()) {
        cout << "> ";
    } else {
        cout << client.get_login() << "@" << client.get_ip() << ":" << client.get_cwd() << "$ ";
    }
    cout.flush();
}

void * async_put(void *data_raw) {
    GetPutData *put_data = (GetPutData *) data_raw;

    try {
        switch (put_data->client.put(put_data->file, put_data->local_file)) {
            case proto::SUCCESS:
                cout << "File " << put_data->local_file << " uploaded succesfully\n";
                break;
            default:
                cout << "error uploading file\n";
                break;
        }
    } catch (std::logic_error &e) {
        cout << "File doesn't exist\n";
    }

    print_greeting(put_data->client);
    delete put_data;
    return nullptr;
}

void * async_get(void *data_raw) {
    GetPutData *put_data = (GetPutData *) data_raw;
    // TODO do it under mutex
    try {
        switch (put_data->client.get(put_data->file, put_data->local_file)) {
            case proto::SUCCESS:
                cout << "File " << put_data->local_file << " downloaded succesfully\n";
                break;
            default:
                cout << "error downloading file\n";
                break;
        }
    } catch (std::logic_error &e) {
        cout << "File doesn't exist\n";
    }

    print_greeting(put_data->client);
    delete put_data;
    return nullptr;
}

void process_put(Client &client) {
    GetPutData *data = new GetPutData(client);

    cin >> data->local_file;
    cin >> data->file;
    // TODO do it under mutex
    pthread_t putter;
    if (pthread_create(&putter, NULL, async_put, data)) {
        cout << "Error creating put processing thread\n";
    }
}


void process_get(Client &client) {
    GetPutData *data = new GetPutData(client);

    cin >> data->file;
    cin >> data->local_file;
    pthread_t getter;
    if (pthread_create(&getter, NULL, async_get, data)) {
        cout << "Error creating get processing thread\n";
    }
    pthread_detach(getter);
}

void process_del(Client &client) {
    string file;
    cin >> file;

    switch (client.del(file)) {
        case proto::SUCCESS:
            break;
        default:
            cout << "Error deleting file\n";
    }
}

void process_pwd(Client &client) {
    string cwd;
    switch (client.pwd(cwd)) {
        case proto::SUCCESS:
            cout << cwd << endl;
            break;
        default:
            cout << "not connected\n";
    }
}

int main(int argc, char **argv) {
    el::Configurations conf("clientlogger.conf");
    el::Loggers::reconfigureAllLoggers(conf);
    cout << "Welcome to vfs Client!\n";

    const char* ip = default_ip;
    int port = default_port;

    if (argc > 1) {
        ip = argv[1];

        if (argc > 2) {
            try {
                port = std::stoi(argv[2]);
            } catch (std::invalid_argument &e) {
                cerr << "Port should be integral value, got " << argv[2] << endl;
                return -1;
            }
        }
    }

    Client client(ip, port);
    while (true) {
        print_greeting(client);

        string command;
        cin >> command;

        if (command == "connect") {
            process_connect(client);
        } else if (command == "exit") {
            client.disconnect();
            break;
        } else if (command == "ls") {
            process_ls(client);
        } else if (command == "cd") {
            process_cd(client);
        } else if (command == "put") {
            process_put(client);
        } else if (command == "get") {
            process_get(client);
        } else if (command == "del") {
            process_del(client);
        } else if (command == "pwd") {
            process_pwd(client);
        } else if (command == "help") {
            print_help();
        }

    }

    return 0;
}

void process_cd(Client &client) {
    string directory;

    cin >> directory;

    try {
        switch (client.cd(directory)) {
            case proto::SUCCESS:
                break;
            default:
                cout << "Directory doesn't exist\n";
                break;
        }
    } catch (std::logic_error &e) {
        cout << e.what() << endl;
    }
}