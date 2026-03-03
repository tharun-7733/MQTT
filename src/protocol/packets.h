#pragma once
#include <vector>
#include <string>
#include "mqttTypes.h"

namespace mqtt {
    // Connect packet
    // This represents the connect message
    struct ConnectPacket
    {
        bool cleanSession{true};
        uint16_t keepAlive{60};
        std::string clientId;
        std::string username;
        std::string password;
    };
    
    // This represents the Connack packet
    // which means gives acknowledgement after receiving the packet
    struct ConnackPacket
    {
        bool sessionPresent{false};
        uint8_t returnCode{0};
    };

    // This represents publishing the message
    struct PublishPacket
    {
        std::string topic;
        std::vector<uint8_t> payload;
        uint16_t packetId{0};
        mqtt::QoS qos{mqtt::QoS::AT_MOST_ONCE};
        bool retain{false};
    };

    // client requests for topics
    struct SubscribePacket
    {
        uint16_t oacketId{0};
        std::vector<std::pair<std::string, mqtt::QoS>> topics;
    };
    // broker response to subscription
    struct SubackPacket
    {
        uint16_t packetId{0};
        std::vector<std::pair<std::string, mqtt::QoS>> topics;
    };
    // Acknowledgement packet
    struct AckPacket
    {
        mqtt::PacketTypes type;
        uint16_t packetId{0};
    };

}
