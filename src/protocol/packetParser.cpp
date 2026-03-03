#include "packetParser.h"

mqtt::ConnectPacket mqtt::PacketParser::parseConnect(
        const std::vector<uint8_t>& data)
{
    mqtt::ConnectPacket packet;

    packet.clientId = "test";
    packet.keepAlive = 60;
    packet.cleanSession = true;

    return packet;
}