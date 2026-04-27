#include <cstdlib>
#include <cstring>
#include "mqtt.hpp"
#include "pack.hpp"

/*
 * MQTT v3.1.1 standard, Remaining length field on the fixed header can be at
 * most 4 bytes.
 */
static const int MAX_LEN_BYTES = 4;

int mqtt_encode_length(uint8_t *buf, size_t len) {
    int bytes = 0;
    do {
        if (bytes + 1 > MAX_LEN_BYTES)
            return bytes;
        short d = len % 128;
        len /= 128;
        if (len > 0)
            d |= 128;
        buf[bytes++] = d;
    } while (len > 0);
    return bytes;
}

unsigned long long mqtt_decode_length(const uint8_t **buf) {
    char c;
    int multiplier = 1;
    unsigned long long value = 0LL;
    do {
        c = **buf;
        value += (c & 127) * multiplier;
        multiplier *= 128;
        (*buf)++;
    } while ((c & 128) != 0);
    return value;
}

static size_t unpack_mqtt_connect(const uint8_t *buf,
                                  union mqtt_header *hdr,
                                  union mqtt_packet *pkt) {
    struct mqtt_connect connect = { *hdr };
    // Initialize pointers to nullptr
    connect.payload.client_id = nullptr;
    connect.payload.username = nullptr;
    connect.payload.password = nullptr;
    connect.payload.will_topic = nullptr;
    connect.payload.will_message = nullptr;
    
    pkt->connect = connect;
    const uint8_t *init = buf;
    
    size_t len = mqtt_decode_length(&buf);
    
    buf = init + 8; // Skip to 8th byte
    
    pkt->connect.byte = unpack_u8(&buf);
    pkt->connect.payload.keepalive = unpack_u16(&buf);
    uint16_t cid_len = unpack_u16(&buf);
    if (cid_len > 0) {
        pkt->connect.payload.client_id = new uint8_t[cid_len + 1];
        unpack_bytes(&buf, cid_len, pkt->connect.payload.client_id);
    }
    if (pkt->connect.bits.will == 1) {
        unpack_string16(&buf, &pkt->connect.payload.will_topic);
        unpack_string16(&buf, &pkt->connect.payload.will_message);
    }
    if (pkt->connect.bits.username == 1)
        unpack_string16(&buf, &pkt->connect.payload.username);
    if (pkt->connect.bits.password == 1)
        unpack_string16(&buf, &pkt->connect.payload.password);
    return len;
}

static size_t unpack_mqtt_publish(const uint8_t *buf,
                                  union mqtt_header *hdr,
                                  union mqtt_packet *pkt) {
    struct mqtt_publish publish = { *hdr };
    publish.topic = nullptr;
    publish.payload = nullptr;
    
    pkt->publish = publish;
    size_t len = mqtt_decode_length(&buf);
    pkt->publish.topiclen = unpack_string16(&buf, &pkt->publish.topic);
    uint16_t message_len = len;
    
    if (publish.header.bits.qos > AT_MOST_ONCE) {
        pkt->publish.pkt_id = unpack_u16(&buf);
        message_len -= sizeof(uint16_t);
    }
    
    message_len -= (sizeof(uint16_t) + pkt->publish.topiclen);
    pkt->publish.payloadlen = message_len;
    pkt->publish.payload = new uint8_t[message_len + 1];
    unpack_bytes(&buf, message_len, pkt->publish.payload);
    return len;
}

static size_t unpack_mqtt_subscribe(const uint8_t *buf,
                                    union mqtt_header *hdr,
                                    union mqtt_packet *pkt) {
    struct mqtt_subscribe subscribe = { *hdr };
    subscribe.tuples = nullptr;
    
    size_t len = mqtt_decode_length(&buf);
    size_t remaining_bytes = len;
    
    subscribe.pkt_id = unpack_u16(&buf);
    remaining_bytes -= sizeof(uint16_t);
    
    int i = 0;
    while (remaining_bytes > 0) {
        remaining_bytes -= sizeof(uint16_t);
        subscribe.tuples = static_cast<decltype(subscribe.tuples)>(
            std::realloc(subscribe.tuples, (i + 1) * sizeof(*subscribe.tuples)));
        
        subscribe.tuples[i].topic_len = unpack_string16(&buf, &subscribe.tuples[i].topic);
        remaining_bytes -= subscribe.tuples[i].topic_len;
        subscribe.tuples[i].qos = unpack_u8(&buf);
        remaining_bytes -= sizeof(uint8_t); // bug in original tutorial: len -= sizeof(uint8_t)
        i++;
    }
    subscribe.tuples_len = i;
    pkt->subscribe = subscribe;
    return len;
}

static size_t unpack_mqtt_unsubscribe(const uint8_t *buf,
                                      union mqtt_header *hdr,
                                      union mqtt_packet *pkt) {
    struct mqtt_unsubscribe unsubscribe = { *hdr };
    unsubscribe.tuples = nullptr;
    
    size_t len = mqtt_decode_length(&buf);
    size_t remaining_bytes = len;
    
    unsubscribe.pkt_id = unpack_u16(&buf);
    remaining_bytes -= sizeof(uint16_t);
    
    int i = 0;
    while (remaining_bytes > 0) {
        remaining_bytes -= sizeof(uint16_t);
        unsubscribe.tuples = static_cast<decltype(unsubscribe.tuples)>(
            std::realloc(unsubscribe.tuples, (i + 1) * sizeof(*unsubscribe.tuples)));
        
        unsubscribe.tuples[i].topic_len = unpack_string16(&buf, &unsubscribe.tuples[i].topic);
        remaining_bytes -= unsubscribe.tuples[i].topic_len;
        i++;
    }
    unsubscribe.tuples_len = i;
    pkt->unsubscribe = unsubscribe;
    return len;
}

static size_t unpack_mqtt_ack(const uint8_t *buf,
                              union mqtt_header *hdr,
                              union mqtt_packet *pkt) {
    struct mqtt_ack ack = { *hdr };
    size_t len = mqtt_decode_length(&buf);
    ack.pkt_id = unpack_u16(&buf);
    pkt->ack = ack;
    return len;
}

typedef size_t mqtt_unpack_handler(const uint8_t *,
                                   union mqtt_header *,
                                   union mqtt_packet *);

static mqtt_unpack_handler *unpack_handlers[15] = {
    nullptr,
    unpack_mqtt_connect,
    nullptr,
    unpack_mqtt_publish,
    unpack_mqtt_ack,
    unpack_mqtt_ack,
    unpack_mqtt_ack,
    unpack_mqtt_ack,
    unpack_mqtt_subscribe,
    nullptr,
    unpack_mqtt_unsubscribe,
    unpack_mqtt_ack,
    nullptr,
    nullptr,
    nullptr
};

int unpack_mqtt_packet(const uint8_t *buf, union mqtt_packet *pkt) {
    int rc = 0;
    uint8_t type = *buf;
    union mqtt_header header = { type };
    
    if (header.bits.type == DISCONNECT ||
        header.bits.type == PINGREQ ||
        header.bits.type == PINGRESP) {
        pkt->header = header;
    } else {
        if (header.bits.type < 15 && unpack_handlers[header.bits.type] != nullptr) {
            rc = unpack_handlers[header.bits.type](++buf, &header, pkt);
        }
    }
    return rc;
}

uint8_t *pack_mqtt_packet(const union mqtt_packet *pkt, unsigned type) {
    return nullptr; // Stub for now, as not implemented in part 1 of tutorial
}

union mqtt_header *mqtt_packet_header(uint8_t byte) {
    return nullptr; // Stub
}

struct mqtt_ack *mqtt_packet_ack(uint8_t byte, uint16_t pkt_id) {
    return nullptr; // Stub
}

struct mqtt_connack *mqtt_packet_connack(uint8_t byte, uint8_t c_flags, uint8_t rc) {
    return nullptr; // Stub
}

struct mqtt_suback *mqtt_packet_suback(uint8_t byte, uint16_t pkt_id, uint8_t *rcs, uint16_t rcslen) {
    return nullptr; // Stub
}

struct mqtt_publish *mqtt_packet_publish(uint8_t byte, uint16_t pkt_id, size_t topiclen, uint8_t *topic, size_t payloadlen, uint8_t *payload) {
    return nullptr; // Stub
}

void mqtt_packet_release(union mqtt_packet *pkt, unsigned type) {
    if (type == CONNECT) {
        delete[] pkt->connect.payload.client_id;
        delete[] pkt->connect.payload.username;
        delete[] pkt->connect.payload.password;
        delete[] pkt->connect.payload.will_topic;
        delete[] pkt->connect.payload.will_message;
    } else if (type == PUBLISH) {
        delete[] pkt->publish.topic;
        delete[] pkt->publish.payload;
    } else if (type == SUBSCRIBE) {
        if (pkt->subscribe.tuples) {
            for (int i = 0; i < pkt->subscribe.tuples_len; i++) {
                delete[] pkt->subscribe.tuples[i].topic;
            }
            std::free(pkt->subscribe.tuples);
        }
    } else if (type == UNSUBSCRIBE) {
        if (pkt->unsubscribe.tuples) {
            for (int i = 0; i < pkt->unsubscribe.tuples_len; i++) {
                delete[] pkt->unsubscribe.tuples[i].topic;
            }
            std::free(pkt->unsubscribe.tuples);
        }
    }
}
