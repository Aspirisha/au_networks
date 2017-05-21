//
// Created by andy on 5/20/17.
//

#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <iostream>
#include <arpa/inet.h>
#include "au_socket.h"
#include "../easyloggingpp/src/easylogging++.h"
#include "tcp_socket.h"

#define PROTOCOL_ID 253

INITIALIZE_EASYLOGGINGPP

namespace {

sockaddr init_address(const char *server_addr) {
    sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, server_addr, &serv_addr.sin_addr) <= 0) {
        char ip[30];
        if (hostname_to_ip(server_addr, ip)) {
            throw std::logic_error("Couldn't connect");
        }

        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
            throw std::logic_error("Couldn't connect");
        }
    }
    return *(sockaddr*)(&serv_addr);
}
}

au_stream_socket::au_stream_socket(int socket_fd, const sockaddr &peeraddr, int peer_port, int own_port_) :
        socket_fd(socket_fd), own_port(own_port_), peer_port(peer_port), peer_addr(peeraddr) {
   init();
}

void au_stream_socket::init() {
    if (-1 == socket_fd) {
        throw std::logic_error("Invalid socket fd");
    }

    if (own_port >= 0) {
        port_fd = open(get_port_file(own_port).c_str(), O_CREAT | O_EXCL | O_RDONLY, 0644);
        if (port_fd == -1) {
            throw std::logic_error("Port " + std::to_string(own_port) + " already in use");
        }
    } else { // use first free port
        system(("mkdir -p " + get_ports_dir()).c_str());
        LOG(DEBUG) << "trying port " << get_port_file(own_port);
        for (own_port = 0; own_port <= max_port; own_port++) {
            port_fd = open(get_port_file(own_port).c_str(), O_CREAT | O_EXCL | O_RDONLY, 0644);
            if (port_fd != -1) {
                break;
            }
        }
        if (own_port == max_port + 1) { // fail
            throw std::logic_error("Couldn't find free own_port");
        }
    }
}

au_stream_socket::au_stream_socket(int socket_fd, const char *peeraddr, int peer_port, int own_port_)  :
        socket_fd(socket_fd), own_port(own_port_), peer_port(peer_port) {
    peer_addr = init_address(peeraddr);
    init();
}

au_stream_socket::~au_stream_socket() {
    close(socket_fd);
    unlink(get_port_file(own_port).c_str());
    close(port_fd);
}

void au_stream_socket::send(const void *buf, size_t size) {
    size_t denom = max_segment_body_size - sizeof(struct ip);
    size_t packets = (size + denom - 1) / denom;

    int acked = 0;
    int awaiting_ack = 0;

    size_t written_to_sendbuffer = 0;
    struct pollfd fds;

    fds.fd = socket_fd;
    fds.revents = 0;
    fds.events = POLLIN | POLLOUT;

    constexpr size_t response_size = sizeof(AckResponse) + sizeof(ip);
    constexpr size_t max_responses_bytes = response_size * max_segments_num;
    uint8_t *response = new uint8_t[max_responses_bytes];
    while (acked < packets) {
        size_t to_write = std::min(size - written_to_sendbuffer, max_segment_body_size);
        while (current_window > awaiting_ack && written_to_sendbuffer < size && send_buffer.write(
                (uint8_t *) buf, to_write, own_port, peer_port)) {
            written_to_sendbuffer += to_write;
        }
        int ret = poll(&fds, 1, 100);
        if (ret == -1) {
            throw std::logic_error("Polling error");
        } else if (ret == 0) { // timeout

        } else {
            int largest_acked = 0;
            if (fds.revents & POLLIN) {
                int read_bytes = read_some(socket_fd, response, response_size * awaiting_ack, &peer_addr);
                int full_responses = read_bytes / sizeof(AckResponse);

                for (int i = 0; i < full_responses; i++) {
                    AckResponse r(response + i * response_size + sizeof(ip));
                    if (r.checksum_ok && send_buffer.set_acked(r.header.seq_number)) {
                        acked++;
                        awaiting_ack--;
                        if (r.header.seq_number > largest_acked) {
                            largest_acked = r.header.seq_number;
                            current_window = r.window_size;
                        }
                    }
                }
            }

            if (fds.revents & POLLOUT) {
                auto max_prev_sending_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
                SenderBuffer::AcknowledgeableMsg* msgs[2 * max_segments_num];
                int ready_for_sending = send_buffer.packets_to_send(current_window, msgs, max_prev_sending_time);

                for (int i = 0; i < ready_for_sending; i++) {
                    auto &msg = msgs[i]->msg;
                    auto serialized = msg.serialize();

                    write_fully(socket_fd, serialized.data(), serialized.size(), peer_addr);

                    if (msgs[i]->state == SenderBuffer::NEW) {
                        msgs[i]->state = SenderBuffer::SENT;
                        awaiting_ack++;
                    }
                    msgs[i]->last_send_timestamp = std::chrono::steady_clock::now();
                }
            }

            fds.revents = 0;
        }
    }
}

std::string au_stream_socket::get_ports_dir() {
    std::string home = getenv("HOME");

    return home + "/" + ".ausocketports";
}

std::string au_stream_socket::get_port_file(int port) {
    std::string ports_dir = get_ports_dir();

    return ports_dir + "/" + std::to_string(port);
}

void au_stream_socket::recv(void *buf, size_t size) {
    struct pollfd fds;
    fds.fd = socket_fd;
    fds.revents = 0;
    fds.events = POLLIN | POLLOUT;

    uint8_t *ubuf = (uint8_t *) buf;
    while (size > 0) {
        int ret = poll(&fds, 1, 1000);
        if (ret == -1) {
            throw std::logic_error("Polling error");
        } else if (ret == 0) { // timeout

        } else {
            if (fds.revents & POLLIN) {
                int max_packets_to_receive = recv_buffer.get_free_space();
                for (int i = 0; i < max_packets_to_receive && recv_buffer.ready_to_read() < size; i++) {
                    uint8_t pack[max_ip_packet];
                    read_fully(socket_fd, pack, max_ip_packet, &peer_addr);

                    TransportHeader th(pack + sizeof(ip));
                    TransportDataMessage tdm(th, pack + headers_size);
                    if (tdm.checksum_ok) {
                        recv_buffer.put_message(tdm);
                        send_ack(th.seq_number, own_port, peer_port, std::max(1, max_packets_to_receive - i - 1));
                    }
                }
            }

            int rb = recv_buffer.read(ubuf, size);

            size -= rb;
            ubuf += rb;
            fds.revents = 0;
        }
    }
}

void read_fully(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender, sockaddr &actual_sender) {
    while (size > 0) {
        socklen_t slen = sizeof(sockaddr);

        int rb = recvfrom(socket_fd, s, size, 0, &actual_sender, &slen);
        if (expected_sender) {
            sockaddr_in *es = (sockaddr_in *) expected_sender;
            sockaddr_in *as = (sockaddr_in *) &actual_sender;
            if (es->sin_addr.s_addr != as->sin_addr.s_addr) continue;
        } else {
            expected_sender = &actual_sender; // restrict from now on
        }

        if (rb < 0) {
            throw std::logic_error("Peer closed connection");
        }

        size -= rb;
        s += rb;
    }
}

int read_some(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender) {
    sockaddr actual_sender;
    socklen_t slen = sizeof(sockaddr);
    int rb = recvfrom(socket_fd, s, size, 0, &actual_sender, &slen);
    if (expected_sender) {
        sockaddr_in *es = (sockaddr_in *) expected_sender;
        sockaddr_in *as = (sockaddr_in *) &actual_sender;
        if (es->sin_addr.s_addr != as->sin_addr.s_addr) return 0;
    }
    if (rb < 0) { // hoho
        throw std::logic_error("Peer closed connection");
    }

    return rb;
}

void read_fully(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender) {
    sockaddr dummy;
    read_fully(socket_fd, s, size, expected_sender, dummy);
}

void au_stream_socket::send_ack(int seq_num, int port, int peer_port, int window_size) {
    AckResponse response(port, peer_port, seq_num, window_size);
    auto s = response.serialize();

    write_fully(socket_fd, s.data(), s.size(), peer_addr);
}

void au_stream_socket::connect() {
    TransportHeader msg(own_port, peer_port, 0, 0, TransportHeader::CONNECT);

    msg.count_checksum_separate(nullptr, 0);

    auto s = msg.serialize();
    if (sendto(socket_fd, s.data(), s.size(), 0, &peer_addr, sizeof(struct sockaddr)) < 0)  {
        printf("sendto() failed!\n");
        LOG(ERROR) << strerror(errno);
    }

}


void write_fully(int socket_fd, uint8_t *s, size_t size, const sockaddr &sa) {
    while (size > 0) {
        int wb = sendto(socket_fd, s, size, 0, &sa, sizeof(sockaddr));

        if (wb < 0) {
            throw std::logic_error("Peer closed connection");
        }

        size -= wb;
        s += wb;
    }
}


int ReceiveBuffer::ReadBuffer::read(uint8_t *s, uint32_t size) {
    int copied = 0;
    while (copied < size && read_index < buffer_size && packets[read_index].read_offset != -1) {
        uint32_t packet_size = packets[read_index].msg.header.size - packets[read_index].read_offset;

        int to_copy = std::min(size - copied, packet_size);
        memcpy(s, packets[read_index].msg.data + packets[read_index].read_offset, to_copy);
        packets[read_index].read_offset += to_copy;
        if (packets[read_index].read_offset == packets[read_index].msg.header.size) read_index++;
        copied += to_copy;
        ready_to_read -= to_copy;
    }
    return copied;
}

bool ReceiveBuffer::ReadBuffer::put_message(const TransportDataMessage &msg) {
    if (msg.header.seq_number < base || msg.header.seq_number >= base + buffer_size) return false;

    int id = msg.header.seq_number - base;
    if (packets[id].read_offset != -1) return false;

    packets[id].read_offset = 0;
    packets[id].msg.header = msg.header;
    memcpy(packets[id].data.get(), msg.data, msg.header.size);

    free_packets--;

    while (first_gap < buffer_size && packets[first_gap].read_offset != -1) {
        first_gap++;
        ready_to_read += packets[first_gap].msg.header.size;
    }
    return true;
}

void ReceiveBuffer::ReadBuffer::clear(uint32_t new_base) {
    base = new_base;
    for (int i = 0; i < buffer_size; i++) {
        packets[i].read_offset = -1;
    }
    free_packets = buffer_size;
    first_gap = 0;
    ready_to_read = 0;
}


int ReceiveBuffer::read(uint8_t *s, uint32_t size) {
    int total_read = 0;

    for (int i = 0; i < 2; i++) {
        int rb = buf[cur].read(s + total_read, size - total_read);
        if (buf[cur].get_read_index() == ReadBuffer::buffer_size) {
            std::swap(cur, next);
            buf[next].clear(buf[cur].get_base() + max_segments_num);
        }

        total_read += rb;
    }

    return total_read;
}

int ReceiveBuffer::ready_to_read() const {
    if (buf[cur].first_gap_packet_index() < ReadBuffer::buffer_size) return buf[cur].ready_to_read_bytes();
    return buf[cur].ready_to_read_bytes() + buf[next].ready_to_read_bytes();
}


int SenderBuffer::packets_to_send(int window,
                                  SenderBuffer::AcknowledgeableMsg **msgs,
                                  std::chrono::steady_clock::time_point max_prev_sending_time) {
    using millis = std::chrono::milliseconds;

    int msg_id = 0;
    int passed = 0;

    auto max_time = std::chrono::duration_cast<millis>(max_prev_sending_time.time_since_epoch());
    int set[] = {cur, next};

    for (int index : set) {
        for (int i = buf[index].first_non_acked;
             i < max_segments_num && passed < window; i++) {
            auto &pack = buf[index].packets[i];
            auto time = std::chrono::duration_cast<millis>(
                    pack.last_send_timestamp.time_since_epoch());
            if (pack.state != ACKED && time <= max_time) {
                msgs[msg_id] = &pack;
                msg_id++;
            }
            passed++;
        }
    }

    return msg_id;
}

bool SenderBuffer::set_acked(uint32_t seq_num) {
    bool acked = false;

    buf[next].on_ack(seq_num, acked);
    if (buf[cur].on_ack(seq_num, acked)) {
        std::swap(cur, next);
        buf[next].clear(buf[cur].base + max_segments_num);
    }

    return acked;
}

bool SenderBuffer::write(uint8_t *s, uint32_t size, uint16_t src_prt,
                         uint16_t dest_port) {
    if (buf[cur].size < max_segments_num) {
        buf[cur].push_message(s, size, dest_port, src_prt);
        return true;
    } else if (buf[next].size < max_segments_num) {
        buf[next].push_message(s, size, dest_port, src_prt);
        return true;
    }
    return false;
}


bool SenderBuffer::AckBuffer::on_ack(uint32_t seq_num, bool &acked) {
    if (base > seq_num || base + max_segments_num < seq_num) return false;

    int id = seq_num - base;

    if (packets[id].state == ACKED) return false;
    packets[id].state = ACKED;
    acked = true;
    while (first_non_acked < max_segments_num && packets[first_non_acked].state == ACKED) {
        first_non_acked++;
    }

    return first_non_acked == max_segments_num;
}

void SenderBuffer::AckBuffer::clear(uint32_t new_base) {
    first_non_acked = 0;
    for (int i = 0; i < max_segments_num; i++) {
        packets[i].state = NEW;
    }

    base = new_base;
    size = 0;
}

void SenderBuffer::AckBuffer::push_message(uint8_t *msg, uint32_t sz,
                                           uint16_t dest_port,
                                           uint16_t src_port) {
    packets[size].state = NEW;
    packets[size].msg.data = msg;
    packets[size].msg.header.size = sz;
    packets[size].msg.header.dest_port = dest_port;
    packets[size].msg.header.source_port = src_port;
    packets[size].msg.header.seq_number = size == 0 ? base :
                                          packets[size - 1].msg.header.seq_number + sz;
    packets[size].msg.header.type = TransportHeader::REGULAR;
    packets[size].msg.header.count_checksum_separate(msg, sz);
    size++;
}

TransportHeader::TransportHeader(const uint8_t *s) {
    std::vector<uint8_t> data(sizeof(TransportHeader));
    memcpy(data.data(), s, data.size());

    auto iter = data.cbegin();
    source_port = deserialize_uint16_t(iter);
    dest_port = deserialize_uint16_t(iter);
    seq_number = deserialize_uint32_t(iter);
    checksum = deserialize_uint16_t(iter);
    size = deserialize_uint32_t(iter);
    type = (MessageType) deserialize_uint8_t(iter);
}

std::vector<uint8_t> TransportHeader::serialize() const {
    std::vector<uint8_t> data;
    serialize_uint16_t(source_port, data);
    serialize_uint16_t(dest_port, data);
    serialize_uint32_t(seq_number, data);
    serialize_uint16_t(checksum, data);
    serialize_uint32_t(size, data);
    serialize_uint8_t(type, data);
    return data;
}

bool TransportHeader::validate_checksum_separate(
        const uint8_t *data_without_header) {
    uint16_t prev_checksum = checksum;
    count_checksum_separate(data_without_header, size);

    return checksum == prev_checksum;
}

uint16_t TransportHeader::count_checksum(const uint8_t *data, int size) {
    uint16_t sum = 0;
    for (int i = 0; i + 1 < size; i += 2) {
        sum += (uint16_t(data[i]) << 8) + data[i+1];
    }

    if (size & 1) {
        sum += (uint16_t(data[size - 1]) << 8);
    }

    return sum;
}

void TransportHeader::count_checksum_separate(const uint8_t *data, int size) {
    checksum = 0;
    auto serialized = serialize();
    uint16_t sum = 0;

    for (int i = 0; i + 1 < serialized.size(); i += 2) {
        sum += (uint16_t(serialized[i]) << 8) + serialized[i+1];
    }

    int offset = serialized.size() & 1;
    if (offset) {
        sum += (uint16_t(serialized.back()) << 8) + (size > 0 ? data[0] : 0);
    }

    if (size > offset) {
        sum += count_checksum(data + offset, size - offset);
    }

    checksum = sum;
}

TransportDataMessage::TransportDataMessage(uint16_t source_port, uint16_t dest_port, uint32_t seq_number,
                                           uint32_t size, uint8_t *data)
        : header(source_port, dest_port, seq_number, size, TransportHeader::MessageType::REGULAR),
          data(data) {
    header.count_checksum_separate(data, size);
}

TransportDataMessage::TransportDataMessage() : data(nullptr) {}

TransportDataMessage::TransportDataMessage(TransportHeader &header, uint8_t *data)
        : header(header), data(data) {
    checksum_ok = header.validate_checksum_separate(data);
}

AckResponse::AckResponse(uint16_t source_port, uint16_t dest_port, uint32_t seq_number, uint32_t window_size)
        : header(source_port, dest_port, seq_number, sizeof(uint32_t), TransportHeader::MessageType::ACK),
          window_size(window_size) {
    std::vector<uint8_t> data = header.serialize();
    serialize_uint32_t(window_size, data);
    header.checksum = TransportHeader::count_checksum(data);
}

AckResponse::AckResponse(const uint8_t *s) : header(s) {
    s += sizeof(TransportHeader);
    checksum_ok = header.validate_checksum_separate(s);

    window_size = deserialize_uint32_t(s);
}

std::vector<uint8_t> AckResponse::serialize() const {
    auto s = header.serialize();
    serialize_uint32_t(window_size, s);

    return s;
}

void ReceiveBuffer::ReadBuffer::set_base(int base) {
    this->base = base;
}

int ReceiveBuffer::ReadBuffer::get_base() const {
    return base;
}

int ReceiveBuffer::ReadBuffer::first_gap_packet_index() const {
    return first_gap;
}

int ReceiveBuffer::ReadBuffer::ready_to_read_bytes() const {
    return ready_to_read;
}

int ReceiveBuffer::ReadBuffer::get_free_packets() const {
    return free_packets;
}

int ReceiveBuffer::ReadBuffer::get_read_index() const {
    return read_index;
}

ReceiveBuffer::ReadableMessage::ReadableMessage() {
    data.reset(new uint8_t[max_segment_body_size]);

    msg.data = data.get();
}

ReceiveBuffer::ReceiveBuffer() {
    buf[1].set_base(max_segments_num);
}

au_stream_client_socket::au_stream_client_socket(const sockaddr &peer_addr, uint16_t port) :
        sock(socket(AF_INET, SOCK_RAW, PROTOCOL_ID), peer_addr, port), port(port) { }

au_stream_client_socket::au_stream_client_socket(const char *server_addr,
                                                 uint16_t port) :
        sock(socket(AF_INET, SOCK_RAW, PROTOCOL_ID), server_addr, port), port(port) { }


void au_stream_client_socket::connect() {
    sock.connect();
}

void au_stream_client_socket::send(const void *buf, size_t size) {
    sock.send(buf, size);
}

void au_stream_client_socket::recv(void *buf, size_t size) {
    sock.recv(buf, size);
}


au_stream_server_socket::au_stream_server_socket(const char *addr, int port) : port(port) {
    socket_fd = socket(AF_INET, SOCK_RAW, PROTOCOL_ID);

   // const char *device = "eth0";
    //int status = setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, device, strlen(device));
    if (-1 == socket_fd) {
        LOG(ERROR) << "Couldn't create socket";
        throw std::logic_error("Error creating socket");
    }

}

stream_socket *au_stream_server_socket::accept_one_client() {
    while (true) {
        uint8_t hdr[sizeof(ip) + sizeof(TransportHeader)];
        sockaddr sender;
        read_fully(socket_fd, hdr, sizeof(ip) + sizeof(TransportHeader), nullptr, sender);

        ip *p = (ip *) hdr;

        char addr[30];

        inet_ntop(AF_INET, &p->ip_src, addr, 25);
        LOG(DEBUG) << "ip->v = " << p->ip_v;
        LOG(DEBUG) << "ip->tos = " << p->ip_tos;
        LOG(DEBUG) << "ip->src = " << addr;
        LOG(DEBUG) << "ip->hl = " << p->ip_hl;
        TransportHeader header(hdr + sizeof(ip));

        LOG(DEBUG) << "header port" << header.dest_port;
        LOG(DEBUG) << "header type" << header.type;
        if (!header.validate_checksum_separate(nullptr)) {
            LOG(DEBUG) << "Checksum is wrong";
            continue;
        }

        au_stream_client_socket *sock = new au_stream_client_socket(sender, header.source_port);

        TransportHeader response(sock->get_own_port(), sock->get_peer_port(), 0, 0, TransportHeader::CONNECT_ACK);
        response.count_checksum_separate(nullptr, 0);

        auto s = response.serialize();
        sock->send(s.data(), s.size());
        return sock;
    }

    return nullptr;
}

au_stream_server_socket::~au_stream_server_socket() {

}





