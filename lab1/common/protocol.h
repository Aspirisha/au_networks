//
// Created by andy on 3/21/17.
//

#ifndef LAB1_PROTOCOL_H
#define LAB1_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace proto
{
enum MessageType : uint8_t
{
    CONNECT,
    CD,
    LS,
    GET,
    PUT,
    DEL
};

enum ServerErrorCode {
    SUCCESS,
    INVALID_OPERATION,
    INVALID_PASSWORD,
    CLIENT_ALREADY_CONNECTED
};

struct LengthPrefixedMessage {
    const uint32_t length;
    const void *length_prefixed_data;

    static constexpr size_t length_size = sizeof(uint32_t);
};

struct Message {
    virtual ~Message() {}
    virtual LengthPrefixedMessage serialize() = 0;

    static LengthPrefixedMessage serialize_string();
    virtual MessageType type() const = 0;
protected:
    virtual uint32_t evaluate_total_serialized_size() const = 0;
};

struct ClientMessage : public Message {
    static std::shared_ptr<ClientMessage> deserialize(LengthPrefixedMessage msg);

};

struct ConnectMessage : public ClientMessage {
    ConnectMessage(const std::string &directory,
                   const std::string &password);
    ConnectMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::string directory;
    std::string password;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct CdMessage : public ClientMessage {
    CdMessage(const std::string &directory);
    CdMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::string directory;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct LsMessage : public ClientMessage {
    LsMessage();
    LsMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct GetMessage : public ClientMessage {
    GetMessage(const std::string &src_file);
    GetMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::string src_file;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct PutMessage : public ClientMessage {
    PutMessage(const std::string &dst_file, LengthPrefixedMessage file_data);
    PutMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::string dst_file;
    LengthPrefixedMessage file_data;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct DelMessage : public ClientMessage {
    DelMessage(const std::string &filename);
    DelMessage(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::string filename;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct ServerMessage : public Message {
    static std::shared_ptr<ServerMessage> deserialize(LengthPrefixedMessage msg);
    ServerErrorCode error;
};

struct ConnectResponse : public ServerMessage {
    ConnectResponse(ServerErrorCode error);
    ConnectResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct CdResponse : public ServerMessage {
    CdResponse(ServerErrorCode error, const std::string &new_directory);
    CdResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::string new_directory;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct LsResponse : public ServerMessage {
    LsResponse(ServerErrorCode error, const std::vector<std::string> &files);
    LsResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    std::vector<std::string> files;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct GetResponse : public ServerMessage {
    GetResponse(ServerErrorCode error, LengthPrefixedMessage file_data);
    GetResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;

    LengthPrefixedMessage file_data;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct PutResponse : public ServerMessage {
    PutResponse(ServerErrorCode error);
    PutResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

struct DelResponse : public ServerMessage {
    DelResponse(ServerErrorCode error);
    DelResponse(LengthPrefixedMessage serialized);
    LengthPrefixedMessage serialize() override;
    MessageType type() const override;
protected:
    uint32_t evaluate_total_serialized_size() const override;
};

}

#endif //LAB1_PROTOCOL_H
