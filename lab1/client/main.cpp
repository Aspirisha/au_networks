//
// Created by andy on 3/21/17.
//

#include <iostream>
#include <memory>
#include "../common/tcp_socket.h"
#include "../common/protocol.h"
#include "Client.h"

using namespace std;

namespace {
const int default_port = 40001;
const char *default_ip = "127.0.0.1";
}

void print_help() {
    cout << "Available commands: \n";
    cout << "\thelp -- print this help\n";
    cout << "\texit -- exit form the program\n";
    cout << "\tconnect <login> <password> -- connect to the server vfs with give credentials\n";
    cout << "\tpwd -- print current vfs directory\n";
    cout << "\tls -- print current vfs directory content\n";
    cout << "\tcd <path> -- change current vfs directory\n";
    cout << "\tget <filename> <local file> -- download file from server and save it to <local filename>\n";
    cout << "\tput <local filename> <filename> -- upload <local filename> to server\n";
}

int main(int argc, char **argv) {
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
        if (!client.connected()) {
            cout << "> ";
        } else {
            cout << client.get_login() << "@" << ip << ":" << client.get_cwd() << "$ ";
        }

        string command;
        cin >> command;

        if (command == "connect") {
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
                    default:
                        break;
                }
            } catch (std::logic_error &e) {
                cerr << "Error connecting to server\n";
            }
        } else if (command == "exit") {
            break;
        } else if (command == "ls") {
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
            };
        } else if (command == "help") {
            print_help();
        }

    }

    return 0;
}