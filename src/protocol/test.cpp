#include <cassert>
#include <iostream>
#include <vector>
#include <iomanip>

#include "../protocol/remainingLen.h"
#include "../protocol/packetSerializer.h"
#include "../protocol/packetParser.h"

using namespace std;

void testRemainingLength() {
    cout << "Testing Remaining Length...\n";

    vector<uint32_t> testValues = {0, 1, 127, 128, 321, 16384};

    for (auto value : testValues) {
        auto encoded = mqtt::remainingLength::encode(value);

        size_t used = 0;
        auto decoded = mqtt::remainingLength::decode(encoded, used);

        cout << "Value: " << value << " -> Encoded: ";
        for (auto b : encoded)
            cout << setw(2) << setfill('0') << hex << (int)b << " ";
        cout << " -> Decoded: " << dec << decoded << endl;

        assert(decoded == value);
    }

    cout << "Remaining Length OK\n\n";
}

void testConnack() {
    cout << "Testing CONNACK serialization...\n";

    mqtt::ConnackPacket pkt;
    pkt.sessionPresent = false;
    pkt.returnCode = 0;

    auto bytes = mqtt::PacketSerializer::serializeConnack(pkt);

    cout << "Serialized CONNACK: ";
    for (auto b : bytes)
        cout << setw(2) << setfill('0') << hex << (int)b << " ";
    cout << endl;

    assert(bytes.size() == 4);
    assert(bytes[0] == 0x20);
    assert(bytes[1] == 0x02);
    assert(bytes[2] == 0x00);
    assert(bytes[3] == 0x00);

    cout << "CONNACK OK\n\n";
}

void testConnectParsing() {
    cout << "Testing CONNECT parsing...\n";

    // Minimal valid CONNECT packet:
    // 10 0E
    // 00 04 'M' 'Q' 'T' 'T'
    // 04
    // 02
    // 00 3C
    // 00 04 't' 'e' 's' 't'

    vector<uint8_t> connectPacket = {
        0x10, 0x0E,
        0x00, 0x04, 'M','Q','T','T',
        0x04,
        0x02,
        0x00, 0x3C,
        0x00, 0x04, 't','e','s','t'
    };

    auto connect = mqtt::PacketParser::parseConnect(connectPacket);

    cout << "Parsed Client ID: " << connect.clientId << endl;
    cout << "Parsed KeepAlive: " << connect.keepAlive << endl;
    cout << "Parsed CleanSession: " << connect.cleanSession << endl;

    assert(connect.clientId == "test");
    assert(connect.keepAlive == 60);
    assert(connect.cleanSession == true);

    cout << "CONNECT OK\n\n";
}

int main() {

    testRemainingLength();
    testConnack();
    testConnectParsing();

    cout << "All protocol tests passed.\n";

    return 0;
}