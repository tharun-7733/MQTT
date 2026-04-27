#ifndef MQTT_HPP
#define MQTT_HPP

#include <cstdint>
#include <cstddef>

#define MQTT_HEADER_LEN 2
#define MQTT_ACK_LEN    4

/*
 * Stub bytes, useful for generic replies, these represent the first byte in
 * the fixed header
 */
#define CONNACK_BYTE  0x20
#define PUBLISH_BYTE  0x30
#define PUBACK_BYTE   0x40
#define PUBREC_BYTE   0x50
#define PUBREL_BYTE   0x60
#define PUBCOMP_BYTE  0x70
#define SUBACK_BYTE   0x90
#define UNSUBACK_BYTE 0xB0
#define PINGRESP_BYTE 0xD0

/* Message types */
enum packet_type {
    CONNECT     = 1,
    CONNACK     = 2,
    PUBLISH     = 3,
    PUBACK      = 4,
    PUBREC      = 5,
    PUBREL      = 6,
    PUBCOMP     = 7,
    SUBSCRIBE   = 8,
    SUBACK      = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK    = 11,
    PINGREQ     = 12,
    PINGRESP    = 13,
    DISCONNECT  = 14
};

enum qos_level { AT_MOST_ONCE, AT_LEAST_ONCE, EXACTLY_ONCE };

union mqtt_header {
    uint8_t byte;
    struct {
        unsigned retain : 1;
        unsigned qos : 2;
        unsigned dup : 1;
        unsigned type : 4;
    } bits;
};

struct mqtt_connect {
    union mqtt_header header;
    union {
        uint8_t byte;
        struct {
            unsigned reserved : 1;
            unsigned clean_session : 1;
            unsigned will : 1;
            unsigned will_qos : 2;
            unsigned will_retain : 1;
            unsigned password : 1;
            unsigned username : 1;
        } bits;
    };
    struct {
        uint16_t keepalive;
        uint8_t *client_id;
        uint8_t *username;
        uint8_t *password;
        uint8_t *will_topic;
        uint8_t *will_message;
    } payload;
};

struct mqtt_connack {
    union mqtt_header header;
    union {
        uint8_t byte;
        struct {
            unsigned session_present : 1;
            unsigned reserved : 7;
        } bits;
    };
    uint8_t rc;
};

struct mqtt_subscribe {
    union mqtt_header header;
    uint16_t pkt_id;
    uint16_t tuples_len;
    struct {
        uint16_t topic_len;
        uint8_t *topic;
        unsigned qos;
    } *tuples;
};

struct mqtt_unsubscribe {
    union mqtt_header header;
    uint16_t pkt_id;
    uint16_t tuples_len;
    struct {
        uint16_t topic_len;
        uint8_t *topic;
    } *tuples;
};

struct mqtt_suback {
    union mqtt_header header;
    uint16_t pkt_id;
    uint16_t rcslen;
    uint8_t *rcs;
};

struct mqtt_publish {
    union mqtt_header header;
    uint16_t pkt_id;
    uint16_t topiclen;
    uint8_t *topic;
    uint16_t payloadlen;
    uint8_t *payload;
};

struct mqtt_ack {
    union mqtt_header header;
    uint16_t pkt_id;
};

typedef struct mqtt_ack mqtt_puback;
typedef struct mqtt_ack mqtt_pubrec;
typedef struct mqtt_ack mqtt_pubrel;
typedef struct mqtt_ack mqtt_pubcomp;
typedef struct mqtt_ack mqtt_unsuback;
typedef union mqtt_header mqtt_pingreq;
typedef union mqtt_header mqtt_pingresp;
typedef union mqtt_header mqtt_disconnect;

union mqtt_packet {
    struct mqtt_ack ack;
    union mqtt_header header;
    struct mqtt_connect connect;
    struct mqtt_connack connack;
    struct mqtt_suback suback;
    struct mqtt_publish publish;
    struct mqtt_subscribe subscribe;
    struct mqtt_unsubscribe unsubscribe;
};

int mqtt_encode_length(uint8_t *, size_t);
unsigned long long mqtt_decode_length(const uint8_t **);
int unpack_mqtt_packet(const uint8_t *, union mqtt_packet *);
uint8_t *pack_mqtt_packet(const union mqtt_packet *, unsigned);

union mqtt_header *mqtt_packet_header(uint8_t);
struct mqtt_ack *mqtt_packet_ack(uint8_t , uint16_t);
struct mqtt_connack *mqtt_packet_connack(uint8_t, uint8_t, uint8_t);
struct mqtt_suback *mqtt_packet_suback(uint8_t, uint16_t, uint8_t *, uint16_t);
struct mqtt_publish *mqtt_packet_publish(uint8_t, uint16_t, size_t, uint8_t *, size_t, uint8_t *);
void mqtt_packet_release(union mqtt_packet *, unsigned);

#endif // MQTT_HPP
