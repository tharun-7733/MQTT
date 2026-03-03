#pragma once
#include <vector>
#include "packets.h"

namespace mqtt{
class PacketParser{
    public:
        static ConnectPacket parseConnect(const std::vector<uint8_t>& data);
        static PublishPacket parsePublish(const std::vector<uint8_t>& data);
    };
}