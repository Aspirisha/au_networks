//
// Created by andy on 5/20/17.
//

#ifndef LAB1_AU_SOCKET_H
#define LAB1_AU_SOCKET_H

#include <vector>
#include <cstring>
#include <chrono>
#include <memory>
#include <netinet/ip.h>
#include "stream_socket.h"
#include "persistence.h"

void read_fully(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender, sockaddr &actual_sender);
void read_fully(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender);
int read_some(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender);
void write_fully(int socket_fd, uint8_t *s, size_t size, const sockaddr &sa);

struct __attribute__ ((packed)) TransportHeader {
    enum MessageType : uint8_t {
        REGULAR = 0,
        ACK = 1,
        CONNECT = 2,
        CONNECT_ACK = 3,
        UNKNOWN = 255
    };

    uint16_t source_port;
    uint16_t dest_port;
    uint32_t seq_number; /* This is packet number, numbering for given socket goes from 0 and increases monotonically */
    uint16_t checksum;   /* Checksum for the header AND the message which accompany it; should be evaluated with checksum field = 0*/
    uint32_t size;   // actual message body size
    MessageType type;

    TransportHeader() : type(UNKNOWN) {}

    TransportHeader(uint16_t source_port, uint16_t dest_port, uint32_t seq_number,
                    uint32_t size, MessageType type) :
        source_port(source_port), dest_port(dest_port), seq_number(seq_number),
        checksum(0), size(size), type(type) { }

    TransportHeader(const uint8_t *s);

    std::vector<uint8_t> serialize() const;

    bool validate_checksum_separate(const uint8_t *data_without_header);

    static uint16_t count_checksum(const uint8_t *data, int size);

    void count_checksum_separate(const uint8_t *data, int size);

    static uint16_t count_checksum(const std::vector<uint8_t> &data) {
        return count_checksum(data.data(), data.size());
    }
};

struct AckResponse {
    AckResponse(uint16_t source_port, uint16_t dest_port, uint32_t seq_number, uint32_t window_size);

    AckResponse(const uint8_t *s);

    std::vector<uint8_t> serialize() const;

    TransportHeader header;
    uint32_t window_size;

    bool checksum_ok = true;
};

static constexpr size_t max_segments_num = 100;
static constexpr size_t max_ip_packet = 1500;
static constexpr size_t headers_size = sizeof(ip) + sizeof(TransportHeader);
static constexpr size_t max_segment_body_size = max_ip_packet - headers_size;

struct TransportDataMessage {
    TransportDataMessage(uint16_t source_port, uint16_t dest_port, uint32_t seq_number,
                         uint32_t size, uint8_t *data);
    TransportDataMessage();
    TransportDataMessage(TransportHeader &header, uint8_t *data);

    std::vector<uint8_t> serialize() const {
        auto sh = header.serialize();

        int header_size = sh.size();
        sh.resize(header_size + header.size);
        memcpy(sh.data() + header_size, data, header.size);
        return sh;
    }

    TransportHeader header;
    uint8_t *data;

    bool checksum_ok = true;
};

struct SenderBuffer {
    SenderBuffer() {
        buf[1].base = max_segments_num;
    }

    enum MessageState {
        NEW,
        SENT,
        ACKED
    };

    struct AcknowledgeableMsg {
        TransportDataMessage msg;
        MessageState state;
        std::chrono::steady_clock::time_point last_send_timestamp;
    };

    struct AckBuffer {
        AckBuffer() {
            packets = new AcknowledgeableMsg[max_segments_num];
        }

        ~AckBuffer() {
            delete[] packets;
        }

        bool on_ack(uint32_t seq_num, bool &acked);

        void clear(uint32_t new_base);
        void push_message(uint8_t *msg, uint32_t sz, uint16_t dest_port, uint16_t src_port);

        AcknowledgeableMsg *packets;
        int first_non_acked = 0;
        uint32_t base = 0;
        int size = 0;
    };


    int packets_to_send(int window, AcknowledgeableMsg **msgs,
                        std::chrono::steady_clock::time_point max_prev_sending_time);

    bool set_acked(uint32_t seq_num);

    bool write(uint8_t *s, uint32_t size, uint16_t src_prt, uint16_t dest_port);

    static constexpr int max_segment_body_size = 1500 - sizeof(TransportHeader) - sizeof(struct ip);
    static constexpr int max_segment_size = max_segment_body_size + sizeof(TransportHeader);
protected:
    AckBuffer buf[2];

    int cur = 0;
    int next = 1;

    static constexpr int buffer_size = max_segments_num * max_segment_size;
};


struct ReceiveBuffer {
    struct ReadableMessage {
        ReadableMessage();
        TransportDataMessage msg;

        std::unique_ptr<uint8_t[]> data;
        int read_offset = -1; //
    };

    struct ReadBuffer {
        int read(uint8_t *s, uint32_t size);
        bool put_message(const TransportDataMessage &msg);
        void clear(uint32_t new_base);

        int get_read_index() const;
        int get_free_packets() const;
        int ready_to_read_bytes() const;
        int first_gap_packet_index() const;
        int get_base() const;
        void set_base(int base);
        static constexpr int buffer_size = max_segments_num;

    protected:
        ReadableMessage packets[buffer_size];
        int read_index = 0;
        int ready_to_read = 0;
        int first_gap = 0;
        int base = 0;

        int free_packets = 0;
    };

    ReceiveBuffer();

    int read(uint8_t *s, uint32_t size);

    int ready_to_read() const;

    bool put_message(const TransportDataMessage &msg) {
        return buf[cur].put_message(msg) || buf[next].put_message(msg);
    }

    int get_free_space() const {
        return buf[cur].get_free_packets() + buf[next].get_free_packets();
    }
protected:

    ReadBuffer buf[2];
    int cur = 0;
    int next = 1;
};

class au_stream_socket : public stream_socket {
public:
    au_stream_socket(int socket_fd, const sockaddr &peer_addr, int peer_port, int own_port = -1);
    au_stream_socket(int socket_fd, const char *peer_addr, int peer_port, int own_port = -1);
    ~au_stream_socket();

    void connect();
    void send(const void *buf, size_t size) override;
    void recv(void *buf, size_t size) override;

    uint16_t get_own_port() const { return own_port; }
protected:

    void init();
    static std::string get_port_file(int port);

    static std::string get_ports_dir();

    int socket_fd;
    int port_fd;
    int own_port;
    const int peer_port;
    sockaddr peer_addr;
    SenderBuffer send_buffer;
    ReceiveBuffer recv_buffer;

    size_t current_window = 1500;
    static const size_t default_mtu = 1500;
    static const int max_port = 65535;
    static const int receive_buffer_size = 65535;

    void send_ack(int seq_num, int port, int peer_port, int max);
};

class au_stream_client_socket : virtual public stream_client_socket {
public:
    au_stream_client_socket(const char *server_addr, uint16_t port);
    au_stream_client_socket(const sockaddr &sever_addr, uint16_t port);
    void connect() override;
    void send(const void *buf, size_t size) override;
    void recv(void *buf, size_t size) override;

    uint16_t get_own_port() const { return sock.get_own_port(); }
    uint16_t get_peer_port() const { return port; }
protected:
    au_stream_socket sock;
    uint16_t port;
};


class au_stream_server_socket : public stream_server_socket {
public:
    au_stream_server_socket(const char *addr, int port);
    stream_socket* accept_one_client();
    ~au_stream_server_socket();
protected:
    int socket_fd;
    int port;
};

#endif //LAB1_AU_SOCKET_H
