//
// Created by andy on 3/21/17.
//


#include <netinet/in.h>
#include <iostream>
#include "protocol.h"
#include "persistence.h"

namespace proto
{

std::shared_ptr<ClientMessage> ClientMessage::deserialize(
        LengthPrefixedMessage msg) {
    MessageType type = MessageType(msg.length_prefixed_data[LengthPrefixedMessage::length_size]);

    switch (type) {
        case CONNECT:
            return std::shared_ptr<ClientMessage>(new ConnectMessage(msg));
        case CD:
            return std::shared_ptr<ClientMessage>(new CdMessage(msg));
        case LS:
            return std::shared_ptr<ClientMessage>(new LsMessage(msg));
        case GET:
            return std::shared_ptr<ClientMessage>(new GetMessage(msg));
        case PUT:
            return std::shared_ptr<ClientMessage>(new PutMessage(msg));
        case DEL:
            return std::shared_ptr<ClientMessage>(new DelMessage(msg));
        case PWD:
            return std::shared_ptr<ClientMessage>(new PwdMessage(msg));
        default:
            return {};
    }
}

std::shared_ptr<ClientMessage> ClientMessage::receive_message(stream_socket *s) {
    std::vector<uint8_t> data(proto::Message::header_length);
    s->recv(data.data(), proto::Message::header_length);

    auto iter = data.cbegin();
    uint64_t len = deserialize_uint64_t(iter);

    data.resize(proto::Message::header_length + len);
    s->recv(data.data() + proto::Message::header_length, len);
    proto::LengthPrefixedMessage lpmsg(len, std::move(data));

    return proto::ClientMessage::deserialize(lpmsg);
}


ConnectMessage::ConnectMessage(const std::string &directory,
                                      const std::string &password) : login(
        directory), password(password) { }

MessageType ConnectMessage::type() const {
    return CONNECT;
}

uint64_t ConnectMessage::evaluate_body_serialized_size() const {
    return sizeof(uint16_t) + login.size() + sizeof(uint16_t) +
           password.size();
}

LengthPrefixedMessage ConnectMessage::serialize() const {
    auto data = serialize_header();

    serialize_string_uint16(login, data.first);
    serialize_string_uint16(password, data.first);

    return LengthPrefixedMessage(data.second, std::move(data.first));
}

ConnectMessage::ConnectMessage(LengthPrefixedMessage serialized) {
    auto iter = std::next(serialized.length_prefixed_data.begin(),
                          Message::header_length);
    login = deserialize_string_uint16(iter);
    password = deserialize_string_uint16(iter);
}


std::pair<std::vector<uint8_t>, uint64_t> Message::serialize_header() const {
    std::vector<uint8_t> data;

    uint64_t length = evaluate_body_serialized_size();
    serialize_uint64_t(evaluate_body_serialized_size(), data);
    serialize_uint8_t(type(), data);

    return {data, length};
}

void Message::send(stream_socket &s) const {
    proto::LengthPrefixedMessage raw = serialize();
    s.send(raw.length_prefixed_data.data(), raw.body_length + header_length);
}


CdMessage::CdMessage(const std::string &directory) : directory(directory) { }

MessageType CdMessage::type() const {
    return CD;
}

CdMessage::CdMessage(LengthPrefixedMessage serialized) {
    auto iter = std::next(serialized.length_prefixed_data.begin(),
                          Message::header_length);
    directory = deserialize_string_uint16(iter);
}

LengthPrefixedMessage CdMessage::serialize() const {
    auto data = serialize_header();
    serialize_string_uint16(directory, data.first);
    return LengthPrefixedMessage(data.second, std::move(data.first));
}

uint64_t CdMessage::evaluate_body_serialized_size() const {
    return sizeof(uint16_t) + directory.size();
}

LsMessage::LsMessage() { }

LsMessage::LsMessage(LengthPrefixedMessage serialized) { }

LengthPrefixedMessage LsMessage::serialize() const {
    auto msg = serialize_header();
    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType LsMessage::type() const {
    return LS;
}

uint64_t LsMessage::evaluate_body_serialized_size() const {
    return 0;
}

GetMessage::GetMessage(const std::string &src_file) : src_file(src_file) { }

GetMessage::GetMessage(LengthPrefixedMessage serialized) {
    auto iter = serialized.body_begin();

    src_file = deserialize_string_uint16(iter);
}

LengthPrefixedMessage GetMessage::serialize() const {
    auto msg = serialize_header();
    serialize_string_uint16(src_file, msg.first);

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType GetMessage::type() const {
    return GET;
}

uint64_t GetMessage::evaluate_body_serialized_size() const {
    return sizeof(uint16_t) + src_file.size();
}


std::vector<uint8_t>::const_iterator LengthPrefixedMessage::body_begin() const {
    return std::next(length_prefixed_data.begin(), Message::header_length);
}

PutMessage::PutMessage(const std::string &dst_file, const std::vector<uint8_t>& file_data) :
        dst_file(dst_file), file_data(file_data) { }

MessageType PutMessage::type() const {
    return PUT;
}

LengthPrefixedMessage PutMessage::serialize() const {
    auto msg = serialize_header();

    serialize_string_uint16(dst_file, msg.first);
    msg.first.insert(msg.first.end(), file_data.begin(),file_data.end());
    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

uint64_t PutMessage::evaluate_body_serialized_size() const {
    return sizeof(uint16_t) + dst_file.size() + file_data.size();
}

PutMessage::PutMessage(LengthPrefixedMessage serialized) {
    auto iter = serialized.body_begin();

    dst_file = deserialize_string_uint16(iter);
    file_data = std::vector<uint8_t>(iter, serialized.length_prefixed_data.end());
}

DelMessage::DelMessage(const std::string &filename) : filename(filename) { }

MessageType DelMessage::type() const {
    return DEL;
}

DelMessage::DelMessage(LengthPrefixedMessage serialized) {
    auto iter = serialized.body_begin();

    filename = deserialize_string_uint16(iter);
}

LengthPrefixedMessage DelMessage::serialize() const {
    auto msg = serialize_header();
    serialize_string_uint16(filename, msg.first);
    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

uint64_t DelMessage::evaluate_body_serialized_size() const {
    return sizeof(uint16_t) + filename.size();
}

std::shared_ptr<ServerMessage> ServerMessage::deserialize(
        LengthPrefixedMessage msg) {
    MessageType type = MessageType(msg.length_prefixed_data[LengthPrefixedMessage::length_size]);

    switch (type) {
        case CONNECT:
            return std::shared_ptr<ServerMessage>(new ConnectResponse(msg));
        case CD:
            return std::shared_ptr<ServerMessage>(new CdResponse(msg));
        case LS:
            return std::shared_ptr<ServerMessage>(new LsResponse(msg));
        case GET:
            return std::shared_ptr<ServerMessage>(new GetResponse(msg));
        case PUT:
            return std::shared_ptr<ServerMessage>(new PutResponse(msg));
        case DEL:
            return std::shared_ptr<ServerMessage>(new DelResponse(msg));
        case PWD:
            return std::shared_ptr<ServerMessage>(new PwdResponse(msg));
        default:
            return {};
    }
}

ServerMessage::ServerMessage(ServerErrorCode error) : error(error) { }

std::pair<std::vector<uint8_t>, uint64_t> ServerMessage::serialize_header() const {
    auto msg = Message::serialize_header();
    serialize_uint8_t(error, msg.first);

    return msg;
}

ServerMessage::ServerMessage(LengthPrefixedMessage serialized) {
    error = (ServerErrorCode) serialized.length_prefixed_data[Message::header_length];
}

std::shared_ptr<ServerMessage> ServerMessage::receive_message(stream_socket &s) {
    std::vector<uint8_t> data(proto::Message::header_length);
    s.recv(data.data(), proto::Message::header_length);

    auto iter = data.cbegin();
    uint64_t len = deserialize_uint64_t(iter);

    data.resize(proto::Message::header_length + len);
    s.recv(data.data() + proto::Message::header_length, len);
    proto::LengthPrefixedMessage lpmsg(len, std::move(data));

    return proto::ServerMessage::deserialize(lpmsg);
}


ConnectResponse::ConnectResponse(ServerErrorCode error) : ServerMessage(error) { }

ConnectResponse::ConnectResponse(LengthPrefixedMessage serialized) :
        ServerMessage(serialized) { }

LengthPrefixedMessage ConnectResponse::serialize() const {
    auto msg = serialize_header();
    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType ConnectResponse::type() const {
    return CONNECT;
}

uint64_t ConnectResponse::evaluate_body_serialized_size() const {
    return sizeof(uint8_t);
}

CdResponse::CdResponse(proto::ServerErrorCode error,
                              const std::string &new_directory) :
        ServerMessage(error), new_directory(new_directory) { }

CdResponse::CdResponse(LengthPrefixedMessage serialized) : ServerMessage(serialized) {
    auto iter = std::next(serialized.body_begin());

    new_directory = deserialize_string_uint16(iter);
}

MessageType CdResponse::type() const {
    return CD;
}

LengthPrefixedMessage CdResponse::serialize() const {
    auto msg = serialize_header();

    serialize_string_uint16(new_directory, msg.first);

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

uint64_t CdResponse::evaluate_body_serialized_size() const {
    return sizeof(uint8_t) + sizeof(uint16_t) + new_directory.size();
}


LsResponse::LsResponse(ServerErrorCode error, const std::vector<std::string> &files)
        : ServerMessage(error), files(files) { }

LsResponse::LsResponse(LengthPrefixedMessage serialized) : ServerMessage(serialized) {
    auto iter = std::next(serialized.body_begin());
    uint32_t files_num = deserialize_uint32_t(iter);
    for (uint32_t j = 0; j < files_num; j++) {
        files.push_back(deserialize_string_uint16(iter));
    }
}

LengthPrefixedMessage LsResponse::serialize() const {
    auto msg = serialize_header();

    serialize_uint32_t(files.size(), msg.first);
    for (const std::string &f: files) {
        serialize_string_uint16(f, msg.first);
    }

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType LsResponse::type() const {
    return LS;
}

uint64_t LsResponse::evaluate_body_serialized_size() const {
    uint64_t sz = sizeof(uint8_t) + sizeof(uint32_t);

    for (const std::string &f: files) {
        sz += sizeof(uint16_t);
        sz += f.size();
    }

    return sz;
}

GetResponse::GetResponse(ServerErrorCode error,
                                const std::vector<uint8_t> &file_data) :
        ServerMessage(error), file_data(file_data) { }

GetResponse::GetResponse(LengthPrefixedMessage serialized) : ServerMessage(serialized) {
    auto iter = std::next(serialized.body_begin());

    file_data = std::vector<uint8_t>(
            iter, serialized.length_prefixed_data.end());
}

LengthPrefixedMessage GetResponse::serialize() const {
    auto msg = serialize_header();

    msg.first.insert(msg.first.end(), file_data.begin(),
                     file_data.end());

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType GetResponse::type() const {
    return GET;
}

uint64_t GetResponse::evaluate_body_serialized_size() const {
    return sizeof(uint8_t) + file_data.size();
}

PutResponse::PutResponse(ServerErrorCode error) : ServerMessage(error) { }

PutResponse::PutResponse(LengthPrefixedMessage serialized) : ServerMessage(serialized) { }

LengthPrefixedMessage PutResponse::serialize() const {
    auto msg = serialize_header();

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType PutResponse::type() const {
    return PUT;
}

uint64_t PutResponse::evaluate_body_serialized_size() const {
    return sizeof(uint8_t);
}

DelResponse::DelResponse(ServerErrorCode error) : ServerMessage(error) { }

DelResponse::DelResponse(LengthPrefixedMessage serialized) : ServerMessage(serialized) { }

LengthPrefixedMessage DelResponse::serialize() const {
    auto msg = serialize_header();

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType DelResponse::type() const {
    return DEL;
}

uint64_t DelResponse::evaluate_body_serialized_size() const {
    return sizeof(uint8_t);
}


uint64_t PwdMessage::evaluate_body_serialized_size() const {
    return 0;
}

PwdMessage::PwdMessage(LengthPrefixedMessage serialized) { }

PwdMessage::PwdMessage() { }

LengthPrefixedMessage PwdMessage::serialize() const {
    auto msg = serialize_header();
    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType PwdMessage::type() const {
    return PWD;
}

PwdResponse::PwdResponse(proto::ServerErrorCode error,
                                const std::string &cwd) : ServerMessage(error), cwd(cwd) { }

PwdResponse::PwdResponse(LengthPrefixedMessage serialized) : ServerMessage(serialized) {
    auto iter = std::next(serialized.body_begin());
    cwd = deserialize_string_uint16(iter);
}

LengthPrefixedMessage PwdResponse::serialize() const {
    auto msg = serialize_header();

    serialize_string_uint16(cwd, msg.first);

    return LengthPrefixedMessage(msg.second, std::move(msg.first));
}

MessageType PwdResponse::type() const {
    return PWD;
}

uint64_t PwdResponse::evaluate_body_serialized_size() const {
    return sizeof(uint8_t) + sizeof(uint16_t) + cwd.size();
}


}



