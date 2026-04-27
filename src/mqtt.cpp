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

/*
 * MQTT packet building functions
 */

union mqtt_header *mqtt_packet_header(uint8_t byte) {
    static union mqtt_header header;
    header.byte = byte;
    return &header;
}

struct mqtt_ack *mqtt_packet_ack(uint8_t byte, uint16_t pkt_id) {
    static struct mqtt_ack ack;
    ack.header.byte = byte;
    ack.pkt_id = pkt_id;
    return &ack;
}

struct mqtt_connack *mqtt_packet_connack(uint8_t byte,
                                         uint8_t cflags,
                                         uint8_t rc) {
    static struct mqtt_connack connack;
    connack.header.byte = byte;
    connack.byte = cflags;
    connack.rc = rc;
    return &connack;
}

struct mqtt_suback *mqtt_packet_suback(uint8_t byte,
                                       uint16_t pkt_id,
                                       uint8_t *rcs,
                                       uint16_t rcslen) {
    struct mqtt_suback *suback = new struct mqtt_suback;
    suback->header.byte = byte;
    suback->pkt_id = pkt_id;
    suback->rcslen = rcslen;
    suback->rcs = new uint8_t[rcslen];
    std::memcpy(suback->rcs, rcs, rcslen);
    return suback;
}

struct mqtt_publish *mqtt_packet_publish(uint8_t byte,
                                         uint16_t pkt_id,
                                         size_t topiclen,
                                         uint8_t *topic,
                                         size_t payloadlen,
                                         uint8_t *payload) {
    struct mqtt_publish *publish = new struct mqtt_publish;
    publish->header.byte = byte;
    publish->pkt_id = pkt_id;
    publish->topiclen = topiclen;
    publish->topic = topic;
    publish->payloadlen = payloadlen;
    publish->payload = payload;
    return publish;
}

/*
 * MQTT packets packing functions
 */

typedef uint8_t *mqtt_pack_handler(const union mqtt_packet *);

static uint8_t *pack_mqtt_header(const union mqtt_header *hdr) {
    uint8_t *packed = new uint8_t[MQTT_HEADER_LEN];
    uint8_t *ptr = packed;
    pack_u8(&ptr, hdr->byte);
    /* Encode 0 length bytes, packets like this have only a fixed header */
    mqtt_encode_length(ptr, 0);
    return packed;
}

static uint8_t *pack_mqtt_ack(const union mqtt_packet *pkt) {
    uint8_t *packed = new uint8_t[MQTT_ACK_LEN];
    uint8_t *ptr = packed;
    pack_u8(&ptr, pkt->ack.header.byte);
    mqtt_encode_length(ptr, MQTT_HEADER_LEN);
    ptr++;
    pack_u16(&ptr, pkt->ack.pkt_id);
    return packed;
}

static uint8_t *pack_mqtt_connack(const union mqtt_packet *pkt) {
    uint8_t *packed = new uint8_t[MQTT_ACK_LEN];
    uint8_t *ptr = packed;
    pack_u8(&ptr, pkt->connack.header.byte);
    mqtt_encode_length(ptr, MQTT_HEADER_LEN);
    ptr++;
    pack_u8(&ptr, pkt->connack.byte);
    pack_u8(&ptr, pkt->connack.rc);
    return packed;
}

static uint8_t *pack_mqtt_suback(const union mqtt_packet *pkt) {
    size_t pktlen = MQTT_HEADER_LEN + sizeof(uint16_t) + pkt->suback.rcslen;
    uint8_t *packed = new uint8_t[pktlen];
    uint8_t *ptr = packed;
    pack_u8(&ptr, pkt->suback.header.byte);
    size_t len = sizeof(uint16_t) + pkt->suback.rcslen;
    int step = mqtt_encode_length(ptr, len);
    ptr += step;
    pack_u16(&ptr, pkt->suback.pkt_id);
    for (int i = 0; i < pkt->suback.rcslen; i++)
        pack_u8(&ptr, pkt->suback.rcs[i]);
    return packed;
}

static uint8_t *pack_mqtt_publish(const union mqtt_packet *pkt) {
    /*
     * Calculate the total length of the packet including header and
     * length field of the fixed header part
     */
    size_t pktlen = MQTT_HEADER_LEN + sizeof(uint16_t) +
        pkt->publish.topiclen + pkt->publish.payloadlen;
    if (pkt->header.bits.qos > AT_MOST_ONCE)
        pktlen += sizeof(uint16_t);
    int remaininglen_offset = 0;
    if ((pktlen - 1) > 0x200000)
        remaininglen_offset = 3;
    else if ((pktlen - 1) > 0x4000)
        remaininglen_offset = 2;
    else if ((pktlen - 1) > 0x80)
        remaininglen_offset = 1;
    pktlen += remaininglen_offset;
    uint8_t *packed = new uint8_t[pktlen];
    uint8_t *ptr = packed;
    pack_u8(&ptr, pkt->publish.header.byte);
    // Total len of the packet excluding fixed header len
    size_t len = pktlen - MQTT_HEADER_LEN - remaininglen_offset;
    int step = mqtt_encode_length(ptr, len);
    ptr += step;
    // Topic len followed by topic name in bytes
    pack_u16(&ptr, pkt->publish.topiclen);
    pack_bytes(&ptr, pkt->publish.topic);
    // Packet id
    if (pkt->header.bits.qos > AT_MOST_ONCE)
        pack_u16(&ptr, pkt->publish.pkt_id);
    // Finally the payload
    pack_bytes(&ptr, pkt->publish.payload);
    return packed;
}

static mqtt_pack_handler *pack_handlers[13] = {
    nullptr,
    nullptr,
    pack_mqtt_connack,
    pack_mqtt_publish,
    pack_mqtt_ack,
    pack_mqtt_ack,
    pack_mqtt_ack,
    pack_mqtt_ack,
    nullptr,
    pack_mqtt_suback,
    nullptr,
    pack_mqtt_ack,
    nullptr
};

uint8_t *pack_mqtt_packet(const union mqtt_packet *pkt, unsigned type) {
    if (type == PINGREQ || type == PINGRESP)
        return pack_mqtt_header(&pkt->header);
    if (type < 13 && pack_handlers[type] != nullptr)
        return pack_handlers[type](pkt);
    return nullptr;
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
