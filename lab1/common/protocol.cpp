//
// Created by andy on 3/21/17.
//


#include <netinet/in.h>
#include "protocol.h"

namespace proto
{

std::shared_ptr<ClientMessage> ClientMessage::deserialize(LengthPrefixedMessage msg) {
    MessageType type = reinterpret_cast<const MessageType *>(msg.length_prefixed_data)[4];

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
        default:
            return {};
    }
}
}
