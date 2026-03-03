#include "remainingLen.h"

std::vector<uint8_t> mqtt::remainingLength::encode(uint32_t length)
{
    std::vector<uint8_t> encoded;

    do {
        uint8_t byte = length % 128;
        length /= 128;

        if (length > 0)
            byte |= 0x80;   // set continuation bit

        encoded.push_back(byte);

    } while (length > 0);

    return encoded;
}

uint32_t mqtt::remainingLength::decode(
        const std::vector<uint8_t>& buffer,
        size_t& used)
{
    uint32_t multiplier = 1;
    uint32_t value = 0;
    used = 0;

    uint8_t encodedByte;

    do {
        if (used >= buffer.size())
            throw std::runtime_error("Malformed Remaining Length");

        encodedByte = buffer[used++];
        value += (encodedByte & 127) * multiplier;

        multiplier *= 128;

        if (multiplier > (128 * 128 * 128 * 128))
            throw std::runtime_error("Remaining Length too large");

    } while ((encodedByte & 128) != 0);

    return value;
}