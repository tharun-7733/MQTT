#ifndef SERVER_HPP
#define SERVER_HPP

#include <cstdint>
#include <ctime>
#include <string>
#include <list>
#include <unordered_map>

/* Forward declaration needed by topic helpers */
struct closure;

/* Error codes returned by recv_packet */
#define ERRCLIENTDC    1
#define ERRPACKETERR   2
#define ERRMAXREQSIZE  3
#define ERREAGAIN      4

/* Handler return codes */
#define REARM_R    0
#define REARM_W    1
#define REARM_NONE (-1)

/* Broker-wide statistics published to $SOL topics */
struct sol_info {
    int       nclients;
    int       nconnections;
    long long start_time;
    long long bytes_recv;
    long long bytes_sent;
    long long messages_sent;
    long long messages_recv;
};

/* State machine for non-blocking I/O */
enum client_status {
    WAITING_HEADER,
    WAITING_LENGTH,
    WAITING_DATA,
    SENDING_DATA
};

/* A client session */
struct session {
    std::list<struct topic *> subscriptions;
    // TODO add pending confirmed messages
};

/* A connected MQTT client */
struct sol_client {
    int         fd;
    std::string client_id;
    uint8_t    *will_topic;
    uint8_t    *will_message;
    struct session session;

    /* I/O State Machine */
    enum client_status status;
    size_t rpos;       // Offset of payload
    size_t read;       // Bytes read so far
    size_t toread;     // Bytes we need to read in total
    uint8_t *rbuf;     // Read buffer
    
    size_t wrote;      // Bytes written so far
    size_t towrite;    // Bytes we need to write in total
    uint8_t *wbuf;     // Write buffer
};

/* One entry in a topic's subscriber list */
struct subscriber {
    struct sol_client *client;
    unsigned           qos;
};

/* A topic node: name (possibly a wildcard pattern) + subscriber list */
struct topic {
    std::string           name;
    std::list<subscriber> subscribers;
};

/* Global broker state */
struct sol {
    std::unordered_map<std::string, struct topic *>      topics;
    std::unordered_map<std::string, struct sol_client *> clients;
    std::unordered_map<std::string, struct closure *>    closures;
};

/* Topic helpers */
struct topic *topic_create(const std::string &name);
void          sol_topic_put(struct sol *, struct topic *);
struct topic *sol_topic_get(struct sol *, const char *);

/* Entry point */
int start_server(const char *addr, const char *port);

#endif // SERVER_HPP
