//
// Created by andy on 3/21/17.
//

#ifndef LAB1_PROTOCOL_H
#define LAB1_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "stream_socket.h"

namespace proto
{
enum MessageType : uint8_t
{
    CONNECT,
    CD,
    LS,
    GET,
    PUT,
    DEL,
    DISCONNECT
};

enum ServerErrorCode : uint8_t {
    SUCCESS,
    INVALID_OPERATION,
    INVALID_PASSWORD,
    CLIENT_ALREADY_CONNECTED
};

struct LengthPrefixedMessage {
    LengthPrefixedMessage() : body_length(0) {}

    LengthPrefixedMessage(uint64_t length, std::vector<uint8_t> &&length_prefixed_data) :
        body_length(length), length_prefixed_data(std::move(length_prefixed_data)) {}

    std::vector<uint8_t>::const_iterator body_begin() const;

    const uint64_t body_length;
    const std::vector<uint8_t> length_prefixed_data;

    static constexpr size_t length_size = sizeof(uint64_t);
};

struct Message {
    virtual ~Message() {}
    virtual LengthPrefixedMessage serialize() const = 0;

    static void serialize_uint32_t(uint32_t s, std::vector<uint8_t> &data);
    static void serialize_uint16_t(uint16_t s, std::vector<uint8_t> &data);
    static void serialize_uint8_t(uint8_t s, std::vector<uint8_t> &data);
    static void serialize_uint64_t(uint64_t s, std::vector<uint8_t> &data);
    static void serialize_string_uint16(const std::string &s, std::vector<uint8_t> &data);

    static uint32_t deserialize_uint32_t(std::vector<uint8_t>::const_iterator &iter);
    static uint16_t deserialize_uint16_t(std::vector<uint8_t>::const_iterator &iter);
    static uint8_t deserialize_uint8_t(std::vector<uint8_t>::const_iterator &iter);
    static uint64_t deserialize_uint64_t(std::vector<uint8_t>::const_iterator &iter);
    static std::string deserialize_string_uint16(
            std::vector<uint8_t>::const_iterator &iter);
    virtual MessageType type() const = 0;

    void send(stream_socket &s) const;

    static constexpr size_t header_length = LengthPrefixedMessage::length_size + sizeof(MessageType);
protected:
    /**
     * Evaluates serialize size without taking header into account
     * Header = [body body_length (uint64_t), message type (uint8_t)]
     */
    virtual uint64_t evaluate_body_serialized_size() const = 0;
    virtual std::pair<std::vector<uint8_t>, uint64_t> serialize_header() const;
};

struct ClientMessage : public Message {
    static std::shared_ptr<ClientMessage> receive_message(stream_socket* s);
private:
    static std::shared_ptr<ClientMessage> deserialize(LengthPrefixedMessage msg);

};

struct ConnectMessage : public ClientMessage {
    ConnectMessage(const std::string &directory,
                   const std::string &password);
    ConnectMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::string directory;
    std::string password;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct CdMessage : public ClientMessage {
    CdMessage(const std::string &directory);
    CdMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::string directory;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct LsMessage : public ClientMessage {
    LsMessage();
    LsMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct GetMessage : public ClientMessage {
    GetMessage(const std::string &src_file);
    GetMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::string src_file;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct PutMessage : public ClientMessage {
    PutMessage(const std::string &dst_file, const std::vector<uint8_t>& file_data);
    PutMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::string dst_file;
    std::vector<uint8_t> file_data;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct DelMessage : public ClientMessage {
    DelMessage(const std::string &filename);
    DelMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::string filename;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct ServerMessage : public Message {
    ServerMessage(ServerErrorCode error);
    ServerMessage(LengthPrefixedMessage msg);

    static std::shared_ptr<ServerMessage> receive_message(stream_socket & s);

    static std::shared_ptr<ServerMessage> deserialize(LengthPrefixedMessage msg);
    ServerErrorCode error;

protected:
    virtual std::pair<std::vector<uint8_t>, uint64_t> serialize_header() const;
};

struct ConnectResponse : public ServerMessage {
    ConnectResponse(ServerErrorCode error);
    ConnectResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct CdResponse : public ServerMessage {
    CdResponse(ServerErrorCode error, const std::string &new_directory);
    CdResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::string new_directory;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct LsResponse : public ServerMessage {
    LsResponse(ServerErrorCode error, const std::vector<std::string> &files);
    LsResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::vector<std::string> files;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct GetResponse : public ServerMessage {
    GetResponse(ServerErrorCode error, const std::vector<uint8_t>& file_data);
    GetResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;

    std::vector<uint8_t> file_data;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct PutResponse : public ServerMessage {
    PutResponse(ServerErrorCode error);
    PutResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

struct DelResponse : public ServerMessage {
    DelResponse(ServerErrorCode error);
    DelResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() const override;
    MessageType type() const override;
protected:
    uint64_t evaluate_body_serialized_size() const override;
};

}

#endif //LAB1_PROTOCOL_H
