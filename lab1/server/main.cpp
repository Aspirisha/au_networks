#include <iostream>
#include <string>
#include <vector>
#include "protocol.h"
#include "tcp_socket.h"
#include "server.h"

using namespace std;

namespace {
const int default_port = 40001;
const char *default_ip = "127.0.0.1";
}



void *process_client(void *stream_socket_void) {
    Server server((stream_socket *)stream_socket_void);

    while (true) {
        server.process_client_message();
    }

    return nullptr;
}

int main(int argc, const char **argv) {
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


    tcp_server_socket server_socket(ip, port);

    vector<pthread_t> threads;
    Server::set_root_directory("clients");
    while (true) {
        stream_socket * client = server_socket.accept_one_client();

        cout << "Client connected\n";
        pthread_t thread;
        if (pthread_create(&thread, NULL, process_client, client)) {
            cerr << "Error creating client processing thread\n";
        }
        threads.push_back(thread);
    }

    Server::save_clients_info();
    return 0;
}