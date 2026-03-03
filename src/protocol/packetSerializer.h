#pragma once
#include <vector>
#include "packets.h"
#include "FixedHeader.h"
// These static functions convert MQTT packet objects into raw byte arrays
// that can be transmitted over the network.
namespace mqtt{
    class PacketSerializer{
        public:
            static std::vector<uint8_t> serializeConnack(const ConnackPacket& packet);
            static std::vector<uint8_t> serializePingResp();
    };
}