#include "packetSerializer.h"

namespace mqtt {
    std::vector<uint8_t> mqtt::PacketSerializer::serializeConnack(
        const ConnackPacket& packet)
    {
        std::vector<uint8_t> bytes;

        // Byte 1: Fixed header (CONNACK = 2 << 4)
        bytes.push_back(0x20);

        // Byte 2: Remaining Length = 2
        bytes.push_back(0x02);

        // Byte 3: Session Present
        bytes.push_back(packet.sessionPresent ? 0x01 : 0x00);

        // Byte 4: Return Code
        bytes.push_back(packet.returnCode);

        return bytes;
    }

    std::vector<uint8_t> PacketSerializer::serializePingResp() {
        std::vector<uint8_t> bytes;
        // TODO: Implement ping response serialization
        return bytes;
    }
}
