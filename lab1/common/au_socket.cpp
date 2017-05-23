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


std::string get_ports_dir() {
    std::string home = getenv("HOME");

    return home + "/" + ".ausocketports";
}

std::string get_port_file(int port) {
    std::string ports_dir = get_ports_dir();

    return ports_dir + "/" + std::to_string(port);
}
}

bool read_exact(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender, int flags = 0);


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

bool au_stream_socket::try_receive_ack() {
    constexpr size_t response_size = AckResponse::serialized_size() + sizeof(ip);

    uint8_t *response = new uint8_t[response_size];
    if (!read_exact(socket_fd, response, response_size, &peer_addr, MSG_DONTWAIT)) {
        return false;
    }

    AckResponse r(response + sizeof(ip));
    if (!message_from_this_channel(r.header)) {
        LOG(DEBUG) << "[Ack waiting] Message not from this channel";
        return false;
    }

    if (r.checksum_ok) {
        advertised_window = r.window_size;
    }

    return send_buffer.set_acked(r.header.seq_number);
}

int au_stream_socket::send_available_segments() {
    auto max_prev_sending_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
    SenderBuffer::AcknowledgeableMsg* msgs[2 * max_segments_num];

    int window_size = std::min(advertised_window, congestion_window);
    int ready_for_sending = send_buffer.packets_to_send(window_size, msgs, max_prev_sending_time);

    int actually_sent = 0;
    for (int i = 0; i < ready_for_sending; i++) {
        auto &msg = msgs[i]->msg;
        auto serialized = msg.serialize();

        int wb = write_some(socket_fd, serialized.data(), serialized.size(), peer_addr);

        if (wb == serialized.size()) {
            if (msgs[i]->state == SenderBuffer::NEW) {
                msgs[i]->state = SenderBuffer::SENT;
                LOG(DEBUG) << "Sent " << serialized.size() << " bytes";
                actually_sent++;
            }
            msgs[i]->last_send_timestamp = std::chrono::steady_clock::now();
        }
    }

    return actually_sent;
}

void au_stream_socket::send(const void *buf, size_t size) {
    size_t packets = (size + max_segment_body_size - 1) / max_segment_body_size;

    int acked = 0;
    int awaiting_ack = 0;

    size_t written_to_sendbuffer = 0;
    struct pollfd fds;

    fds.fd = socket_fd;
    fds.revents = 0;
    fds.events = POLLIN | POLLOUT;

    uint8_t *ubuf = (uint8_t *) buf;

    LOG(DEBUG) << "Started au_stream_socket::send to send " << size << " bytes";
    const int time_to_wait = 100;

    while (acked < packets) {
        size_t to_write = std::min(size - written_to_sendbuffer, max_segment_body_size);
        while (written_to_sendbuffer < size && send_buffer.write(
                ubuf, to_write, own_port, peer_port)) {
            written_to_sendbuffer += to_write;
            ubuf += to_write;

            to_write = std::min(size - written_to_sendbuffer, max_segment_body_size);
        }
        int ret = poll(&fds, 1, time_to_wait);
        if (ret == -1) {
            throw std::logic_error("Polling error");
        } else if (ret == 0) {
            if (awaiting_ack > 0) {
                congestion_window = std::max(1u, congestion_window / 2);
                LOG(DEBUG) << "Decreasing congestion window due to no acks";
            }
        } else {
            int largest_acked = 0;
            if ((advertised_window == 0 || awaiting_ack > 0) && (fds.revents & POLLIN)) {
                int max_acks = std::max(1, awaiting_ack);
                int received_acks = 0;
                for (int i = 0; i < max_acks; i++) {
                    bool received_ack = try_receive_ack();

                    if (received_ack) {
                        acked++;
                        awaiting_ack--;
                        received_acks++;
                        LOG(DEBUG) << "Received ack. Now waiting for " << awaiting_ack << " acks";
                    }
                }

                if (received_acks < awaiting_ack / 2) {
                    simultaneous_acks_to_inc_congestion = std::max(1, simultaneous_acks_to_inc_congestion / 2);
                    congestion_window = std::max(1u, congestion_window / 2);
                    LOG(DEBUG) << "Descreasing congestion window; now it is " << congestion_window;
                }

                if (received_acks >= simultaneous_acks_to_inc_congestion) {
                    simultaneous_acks_to_inc_congestion <<= 1;
                    congestion_window = std::min<uint32_t>(2 * congestion_window, max_segments_num);
                    LOG(DEBUG) << "Increasing congestion window; now it is " << congestion_window;
                }
            }

            if (fds.revents & POLLOUT) {
                awaiting_ack += send_available_segments();
                LOG(DEBUG) << "Now waiting for " << awaiting_ack << " acks";
            }

            fds.revents = 0;
        }
    }
}


void au_stream_socket::recv(void *buf, size_t size) {
    struct pollfd fds;
    fds.fd = socket_fd;
    fds.revents = 0;
    fds.events = POLLIN;

    uint8_t *ubuf = (uint8_t *) buf;
    LOG(DEBUG) << "Started au_stream_socket::recv to read " << size << " bytes";
    while (true) {
        int rb = recv_buffer.read(ubuf, size);

        size -= rb;
        ubuf += rb;

        if (size == 0) {
            LOG(DEBUG) << "read fully, and there are still "  << recv_buffer.ready_to_read() << " bytes in buffer";
            break;
        }

        int ret = poll(&fds, 1, -1);
        if (ret == -1) {
            throw std::logic_error("Polling error");
        } else if (ret == 0) { // timeout

        } else {
            if (fds.revents & POLLIN) {
                int max_packets_to_receive = recv_buffer.get_free_space();
                LOG(DEBUG) << "Can receive up to " << max_packets_to_receive << " packets";

                for (int i = 0; i < max_packets_to_receive && recv_buffer.ready_to_read() < size; i++) {

                    int to_read = headers_size + std::min(size - recv_buffer.ready_to_read(), max_segment_body_size);
                    uint8_t pack[max_ip_packet];
                    LOG(DEBUG) << "Trying to read " << to_read << " bytes";
                    int actually_read = read_data(pack, max_ip_packet, &peer_addr);

                    TransportHeader th(pack + sizeof(ip));

                    if (!message_from_this_channel(th)) {
                        continue;
                    }

                    TransportDataMessage tdm(th, pack + headers_size, actually_read - headers_size);
                    LOG(DEBUG) << "Received header of type " << TransportHeader::type_to_string(th.type);
                    LOG(DEBUG) << "Size written in header is " << th.size << " and actual is " << actually_read - headers_size;
                    if (tdm.checksum_ok) {
                        LOG(DEBUG) << "Received message with seq num " << th.seq_number;
                        if (recv_buffer.put_message(tdm)) {
                            send_ack(th.seq_number, own_port, peer_port, recv_buffer.get_free_space(), th.timestamp);

                            LOG(DEBUG) << "Now ready to read " <<
                            recv_buffer.ready_to_read();
                        } else {
                            LOG(DEBUG) << "Failed to put message into receiver buffer";
                        }
                    }
                }
            }

            fds.revents = 0;
        }
    }
}

int au_stream_server_socket::read_accept(uint8_t *s, size_t size, sockaddr &actual_sender) {
    socklen_t slen = sizeof(sockaddr);

    int rb = recvfrom(socket_fd, s, size, 0, &actual_sender, &slen);

    if (rb < 0) {
        throw std::logic_error("Peer closed connection");
    }

    return rb;
}

int au_stream_socket::read_data(uint8_t *s, size_t size, const sockaddr *expected_sender) {
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

bool read_exact(int socket_fd, uint8_t *s, size_t size, const sockaddr *expected_sender, int flags) {
    sockaddr actual_sender;
    socklen_t slen = sizeof(sockaddr);
    int rb = recvfrom(socket_fd, s, size, MSG_TRUNC | flags, &actual_sender, &slen);
    if (rb != size) return false;

    if (expected_sender) {
        sockaddr_in *es = (sockaddr_in *) expected_sender;
        sockaddr_in *as = (sockaddr_in *) &actual_sender;
        if (es->sin_addr.s_addr != as->sin_addr.s_addr) return false;
    }
    if (rb < 0) { // hoho
        throw std::logic_error("Peer closed connection");
    }

    return true;
}


void au_stream_socket::send_ack(int seq_num, int port, int peer_port, int window_size, uint64_t request_timestamp) {
    AckResponse response(port, peer_port, seq_num, window_size, request_timestamp);
    auto s = response.serialize();

    write_some(socket_fd, s.data(), s.size(), peer_addr);
}

void au_stream_socket::connect() {
    TransportHeader msg(own_port, peer_port, 0, 0, TransportHeader::CONNECT);

    msg.count_checksum_separate(nullptr, 0);

    auto s = msg.serialize();
    if (sendto(socket_fd, s.data(), s.size(), 0, &peer_addr, sizeof(struct sockaddr)) < 0)  {
        printf("sendto() failed!\n");
        LOG(ERROR) << strerror(errno);
    } else {
        LOG(DEBUG) << "Sent connect request";
    }

    uint8_t response[headers_size];
    while (true) {
        if (!read_exact(socket_fd, response, headers_size, &peer_addr)) {
            continue;
        }

        TransportHeader r(response + sizeof(ip));

        LOG(DEBUG) << "Recieved packet of type: " << TransportHeader::type_to_string(r.type);
        if (r.validate_checksum_separate(nullptr, 0) && r.dest_port == own_port && r.type == TransportHeader::CONNECT_ACK) {
            peer_port = r.source_port;
            LOG(DEBUG) << "Transport-level connection finished";
            return;
        }
    }

    throw std::logic_error("Couldn't connect to server");
}

void au_stream_socket::send_unreliable(const void *buf, size_t size) {
    write_some(socket_fd, (uint8_t *) buf, size, peer_addr);
}

bool au_stream_socket::message_from_this_channel(
        const TransportHeader &t) const {
    return t.dest_port == own_port && t.source_port == peer_port;
}

int write_some(int socket_fd, uint8_t *s, size_t size, const sockaddr &sa) {
    int wb = sendto(socket_fd, s, size, 0, &sa, sizeof(sockaddr));

    if (wb < 0) {
        throw std::logic_error("Peer closed connection");
    }

    return wb;
}

int ReceiveBuffer::ReadBuffer::read(uint8_t *s, uint32_t size) {
    int copied = 0;
    while (copied < size && read_index < buffer_size && packets[read_index].read_offset != -1) {
        uint32_t packet_size = packets[read_index].msg.header.size - packets[read_index].read_offset;

        int to_copy = std::min(size - copied, packet_size);
        memcpy(s, packets[read_index].data + packets[read_index].read_offset, to_copy);
        packets[read_index].read_offset += to_copy;
        if (packets[read_index].read_offset == packets[read_index].msg.header.size) {
            read_index++;
        }
        copied += to_copy;
        s += to_copy;
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
    memcpy(packets[id].data, msg.data, msg.header.size);

    free_packets--;

    while (first_gap < buffer_size && packets[first_gap].read_offset != -1) {
        ready_to_read += packets[first_gap].msg.header.size;
        first_gap++;
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
    int total_read = buf[cur].read(s, size);

    if (buf[cur].get_read_index() == ReadBuffer::buffer_size) {
        std::swap(cur, next);
        buf[next].clear(buf[cur].get_base() + max_segments_num);
        total_read += buf[cur].read(s + total_read, size - total_read);
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
             i < max_segments_num && passed < window && i < buf[index].size; i++) {
            auto &pack = buf[index].packets[i];
            auto time = std::chrono::duration_cast<millis>(
                    pack.last_send_timestamp.time_since_epoch());
            if (pack.state != ACKED && time <= max_time && pack.state != EMPTY) {
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
        packets[i].state = EMPTY;
    }

    base = new_base;
    size = 0;
}

void SenderBuffer::AckBuffer::push_message(uint8_t *msg, uint32_t sz,
                                           uint16_t dest_port,
                                           uint16_t src_port) {
    packets[size].state = NEW;
    packets[size].msg.data = msg; // TODO this is safe (not to copy data) since we send everything in a blocking mode
    packets[size].msg.header.size = sz;
    packets[size].msg.header.dest_port = dest_port;
    packets[size].msg.header.source_port = src_port;
    packets[size].msg.header.seq_number = base + size;
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
    timestamp = deserialize_uint64_t(iter);
}

std::vector<uint8_t> TransportHeader::serialize() const {
    std::vector<uint8_t> data;
    serialize_uint16_t(source_port, data);
    serialize_uint16_t(dest_port, data);
    serialize_uint32_t(seq_number, data);
    serialize_uint16_t(checksum, data);
    serialize_uint32_t(size, data);
    serialize_uint8_t(type, data);
    serialize_uint64_t(timestamp, data);
    return data;
}

bool TransportHeader::validate_checksum_separate(
        const uint8_t *data_without_header, size_t expected_size) {
    uint16_t prev_checksum = checksum;

    if (size != expected_size) return false;

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

TransportDataMessage::TransportDataMessage(TransportHeader &header, uint8_t *data, uint32_t data_size)
        : header(header), data(data) {
    checksum_ok = header.validate_checksum_separate(data, data_size) && header.type == TransportHeader::REGULAR;
}

AckResponse::AckResponse(uint16_t source_port, uint16_t dest_port, uint32_t seq_number,
                         uint32_t window_size, uint64_t request_timestamp)
        : header(source_port, dest_port, seq_number, sizeof(uint32_t), TransportHeader::MessageType::ACK),
          window_size(window_size) {
    header.timestamp = request_timestamp;
    std::vector<uint8_t> data = header.serialize();
    serialize_uint32_t(window_size, data);
    header.checksum = TransportHeader::count_checksum(data);
}

AckResponse::AckResponse(const uint8_t *s) : header(s) {
    s += sizeof(TransportHeader);
    checksum_ok = header.validate_checksum_separate(s, sizeof(uint32_t)) && header.type == TransportHeader::ACK;

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
    msg.data = data;
}

ReceiveBuffer::ReceiveBuffer() {
    buf[1].set_base(max_segments_num);
}

au_stream_client_socket::au_stream_client_socket(const sockaddr &server_addr, uint16_t port) :
        sock(socket(AF_INET, SOCK_RAW, PROTOCOL_ID), server_addr, port) { }

au_stream_client_socket::au_stream_client_socket(const char *server_addr,
                                                 uint16_t port) :
        sock(socket(AF_INET, SOCK_RAW, PROTOCOL_ID), server_addr, port) { }


void au_stream_client_socket::connect() {
    sock.connect();
}

void au_stream_client_socket::send(const void *buf, size_t size) {
    sock.send(buf, size);
}

void au_stream_client_socket::recv(void *buf, size_t size) {
    sock.recv(buf, size);
}

void au_stream_client_socket::send_unreliable(const void *buf, size_t size) {
    sock.send_unreliable(buf, size);
}

au_stream_client_socket::au_stream_client_socket(const sockaddr &server_addr,
                                                 uint16_t port,
                                                 au_stream_server_socket *ss, in_addr_t addr) :
        au_stream_client_socket(server_addr, port) {
    server_socket = ss;
    own_address = addr;
}

au_stream_client_socket::~au_stream_client_socket() {
    if (server_socket) {
        server_socket->notify_close(this);
    }
}


au_stream_server_socket::au_stream_server_socket(const char *addr, int port) : port(port) {
    socket_fd = socket(AF_INET, SOCK_RAW, PROTOCOL_ID);
    pthread_rwlock_init(&clients_lock, NULL);

   // const char *device = "eth0";
    //int status = setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, device, strlen(device));
    if (-1 == socket_fd) {
        LOG(ERROR) << "Couldn't create socket";
        throw std::logic_error("Error creating socket");
    }

    port_fd = open(get_port_file(port).c_str(), O_CREAT | O_EXCL | O_RDONLY, 0644);
    if (port_fd == -1) {
        throw std::logic_error("Port " + std::to_string(port) + " already in use");
    }
}

stream_socket *au_stream_server_socket::accept_one_client() {
    while (true) {

        uint8_t hdr[headers_size] = {0};
        sockaddr sender;

        while (headers_size != read_accept(hdr, headers_size, sender));

        TransportHeader header(hdr + sizeof(ip));

        if (header.type != TransportHeader::CONNECT ||
            header.dest_port != port) {
            continue;
        }

        if (!header.validate_checksum_separate(nullptr, 0)) {
            LOG(DEBUG) << "Connect: checksum is wrong";
            continue;
        }

        sockaddr_in *sa = (sockaddr_in *) &sender;
        uint16_t port = header.source_port;
        std::pair<in_addr_t, uint16_t> key(sa->sin_addr.s_addr, port);

        pthread_rwlock_rdlock(&clients_lock);
        auto already_connected = client_sockets.find(key);
        bool contains = already_connected != client_sockets.end();
        pthread_rwlock_unlock(&clients_lock);

        au_stream_client_socket *sock = nullptr;
        if (!contains) {
            LOG(DEBUG) << "Creating new client socket";
            sock = new au_stream_client_socket(sender, header.source_port, this,
                                               sa->sin_addr.s_addr);
            int rc = pthread_rwlock_wrlock(&clients_lock);
            client_sockets.insert({key, sock});
            pthread_rwlock_unlock(&clients_lock);
        } else {
            sock = already_connected->second;
        }

        TransportHeader response(sock->get_own_port(), sock->get_peer_port(), 0,
                                 0, TransportHeader::CONNECT_ACK);
        response.count_checksum_separate(nullptr, 0);

        auto s = response.serialize();
        sock->send_unreliable(s.data(), s.size());
        return sock;
    }
}

au_stream_server_socket::~au_stream_server_socket() {
    pthread_rwlock_destroy(&clients_lock);
}

void au_stream_server_socket::notify_close(
        au_stream_client_socket *client_sock) {

    LOG(DEBUG) << "au_stream_server_socket::notify_close";
    std::pair<in_addr_t, uint16_t> key(client_sock->own_address, client_sock->get_own_port());

    pthread_rwlock_wrlock(&clients_lock);
    client_sockets.erase(key);
    pthread_rwlock_unlock(&clients_lock);
}







