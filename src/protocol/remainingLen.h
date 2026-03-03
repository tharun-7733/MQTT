#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace mqtt{
    class remainingLength {
    public:
        // encoding the number to byte format
        static std::vector<uint8_t> encode(uint32_t length);
        // decoding the byte format into actual number
        static uint32_t decode(const std::vector<uint8_t> &buffer, size_t& used);
    };
}