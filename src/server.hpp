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

/* A connected MQTT client */
struct sol_client {
    int         fd;
    std::string client_id;
    uint8_t    *will_topic;
    uint8_t    *will_message;
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
