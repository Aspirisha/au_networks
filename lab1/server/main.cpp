#include <iostream>
#include <string>
#include <vector>
#include <au_socket.h>
#include "../easyloggingpp/src/easylogging++.h"
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
        try {
            server.process_client_message();
        } catch (std::logic_error &e) {
            cout << e.what() << endl;
            break;
        }
    }

    return nullptr;
}

void *user_info_dumper(void *) {
    while (true) {
        Server::save_clients_info();
        sleep(60);
    }

    return nullptr;
}

int main(int argc, const char **argv) {
    el::Configurations conf("serverlogger.conf");
    el::Loggers::reconfigureAllLoggers(conf);

    const char* ip = default_ip;
    int port = default_port;

    if (argc > 1) {
        ip = argv[1];

        if (argc > 2) {
            try {
                port = std::stoi(argv[2]);
            } catch (std::invalid_argument &e) {
                LOG(ERROR) << "Port should be integral value, got " << argv[2];
                return -1;
            }
        }
    }


    stream_server_socket *server_socket = nullptr;

    const char *sock_type = getenv("STREAM_SOCK_TYPE");
    if (!sock_type || !strcmp(sock_type, "tcp")) {
        LOG(DEBUG) << "Using tcp_stream_socket implementation";
        server_socket = new tcp_server_socket(ip, port);
    } else if (!strcmp(sock_type, "au")) {
        LOG(DEBUG) << "Using au_stream_socket implementation";
        server_socket = new au_stream_server_socket(ip, port);
    } else {
        LOG(ERROR) << "Environment variable $STREAM_SOCK_TYPE defines unknown socket type: "
            << sock_type;
        return 1;
    }

    vector<pthread_t> threads;
    Server::set_root_directory("clients");

    LOG(INFO) << "Starting server with ip " << ip << " and own_port " << port;
    pthread_t persistence_thread;
    if (pthread_create(&persistence_thread, NULL, user_info_dumper, nullptr)) {
        LOG(ERROR) << "Error creating persistence thread";
        return -1;
    }

    while (true) {
        stream_socket * client = server_socket->accept_one_client();

        LOG(INFO) << "Client socket connected";
        pthread_t thread;
        if (pthread_create(&thread, NULL, process_client, client)) {
            LOG(ERROR) << "Error creating Client processing thread";
        }
        threads.push_back(thread);
    }


    return 0;
}