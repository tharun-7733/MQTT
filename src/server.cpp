#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "server.hpp"
#include "network.hpp"
#include "mqtt.hpp"
#include "pack.hpp"
#include "util.hpp"
#include "config.hpp"
#include "trie.hpp"

/* Sol seconds easter egg */
static const double SOL_SECONDS = 88775.24;

/* Global statistics */
static struct sol_info info;

/* Extended global state containing topics trie and clients */
struct sol_extended {
    struct trie topics_trie;
    std::unordered_map<std::string, struct sol_client *> clients;
    std::unordered_map<std::string, struct closure *>    closures;
};
static struct sol_extended sol;

/* I/O closures */
static void on_read(struct evloop *, void *);
static void on_write(struct evloop *, void *);
static void on_accept(struct evloop *, void *);
static void publish_stats(struct evloop *, void *);

/* Topic helpers implementation */
struct topic *topic_create(const std::string &name) {
    struct topic *t = new struct topic;
    t->name = name;
    return t;
}

void topic_add_subscriber(struct topic *t, struct sol_client *client, unsigned qos, bool cleansession) {
    struct subscriber sub;
    sub.client = client;
    sub.qos = qos;
    t->subscribers.push_back(sub);
    if (!cleansession)
        client->session.subscriptions.push_back(t);
}

void topic_del_subscriber(struct topic *t, struct sol_client *client, bool cleansession) {
    t->subscribers.remove_if([client](const subscriber& s) { return s.client->client_id == client->client_id; });
    // TODO: remove from session if cleansession == false
}

void sol_topic_put(struct sol_extended *s, struct topic *t) {
    trie_insert(&s->topics_trie, t->name, t);
}

struct topic *sol_topic_get(struct sol_extended *s, const char *name) {
    std::vector<struct topic *> matches;
    trie_find(&s->topics_trie, std::string(name), matches);
    if (!matches.empty()) return matches[0]; // Just return first match for now
    return nullptr;
}

/* Command handlers */
typedef int handler(struct closure *, union mqtt_packet *);

static int connect_handler(struct closure *cb, union mqtt_packet *pkt) {
    std::string cid = reinterpret_cast<char*>(pkt->connect.payload.client_id);
    if (sol.clients.find(cid) != sol.clients.end()) {
        sol_info("Received double CONNECT from %s, disconnecting client", cid.c_str());
        close(cb->fd);
        sol.clients.erase(cid);
        sol.closures.erase(cb->closure_id);
        info.nclients--;
        info.nconnections--;
        return REARM_NONE; // Return a value that makes on_read drop the client cleanly
    }

    sol_info("New client connected as %s (c%i, k%u)", cid.c_str(),
             pkt->connect.bits.clean_session, pkt->connect.payload.keepalive);

    struct sol_client *new_client = new struct sol_client;
    new_client->fd = cb->fd;
    new_client->client_id = cid;
    new_client->will_topic = nullptr;
    new_client->will_message = nullptr;

    sol.clients[cid] = new_client;
    cb->obj = new_client;

    union mqtt_packet connack;
    unsigned char session_present = 0;
    unsigned char connect_flags = 0 | (session_present & 0x1) << 0;
    unsigned char rc = 0;

    connack.connack = *mqtt_packet_connack(CONNACK_BYTE, connect_flags, rc);
    uint8_t *packed = pack_mqtt_packet(&connack, CONNACK);

    cb->payload = bytestring_create(MQTT_ACK_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
    delete[] packed;

    sol_debug("Sending CONNACK to %s (%u, %u)", cid.c_str(), session_present, rc);

    return REARM_W;
}

static int disconnect_handler(struct closure *cb, union mqtt_packet *pkt) {
    struct sol_client *c = static_cast<struct sol_client *>(cb->obj);
    sol_debug("Received DISCONNECT from %s", c->client_id.c_str());
    close(c->fd);
    sol.clients.erase(c->client_id);
    sol.closures.erase(cb->closure_id);
    info.nclients--;
    info.nconnections--;
    // TODO remove from all topic where it subscribed
    return REARM_NONE;
}

static void recursive_subscription(struct trie_node *node, void *arg) {
    if (!node || !node->topic_ptr) return;
    struct topic *t = node->topic_ptr;
    struct subscriber *s = static_cast<struct subscriber *>(arg);
    t->subscribers.push_back(*s);
}

static int subscribe_handler(struct closure *cb, union mqtt_packet *pkt) {
    struct sol_client *c = static_cast<struct sol_client *>(cb->obj);
    bool wildcard = false;

    unsigned char *rcs = new unsigned char[pkt->subscribe.tuples_len];

    for (unsigned i = 0; i < pkt->subscribe.tuples_len; i++) {
        sol_debug("Received SUBSCRIBE from %s", c->client_id.c_str());
        std::string topic_str = reinterpret_cast<char*>(pkt->subscribe.tuples[i].topic);
        sol_debug("\t%s (QoS %i)", topic_str.c_str(), pkt->subscribe.tuples[i].qos);
        
        if (topic_str.length() >= 2 && topic_str.substr(topic_str.length() - 2) == "/#") {
            topic_str = topic_str.substr(0, topic_str.length() - 1); // remove '#'
            wildcard = true;
        } else if (!topic_str.empty() && topic_str.back() != '/') {
            topic_str += "/";
        }
        
        struct topic *t = sol_topic_get(&sol, topic_str.c_str());
        if (!t) {
            t = topic_create(topic_str);
            sol_topic_put(&sol, t);
        } else if (wildcard) {
            struct subscriber sub;
            sub.client = c;
            sub.qos = pkt->subscribe.tuples[i].qos;
            trie_prefix_map_tuple(&sol.topics_trie, topic_str, recursive_subscription, &sub);
        }

        topic_add_subscriber(t, c, pkt->subscribe.tuples[i].qos, true);
        rcs[i] = pkt->subscribe.tuples[i].qos;
    }

    union mqtt_packet suback;
    struct mqtt_suback *suback_ptr = mqtt_packet_suback(SUBACK_BYTE, pkt->subscribe.pkt_id, rcs, pkt->subscribe.tuples_len);
    suback.suback = *suback_ptr;
    
    unsigned char *packed = pack_mqtt_packet(&suback, SUBACK);
    size_t len = MQTT_HEADER_LEN + sizeof(uint16_t) + pkt->subscribe.tuples_len;
    cb->payload = bytestring_create(len);
    std::memcpy(cb->payload->data, packed, len);
    delete[] packed;
    
    delete[] suback.suback.rcs;
    delete suback_ptr;
    delete[] rcs;

    sol_debug("Sending SUBACK to %s", c->client_id.c_str());
    return REARM_W;
}

static int unsubscribe_handler(struct closure *cb, union mqtt_packet *pkt) {
    struct sol_client *c = static_cast<struct sol_client *>(cb->obj);
    sol_debug("Received UNSUBSCRIBE from %s", c->client_id.c_str());
    
    union mqtt_packet ack;
    ack.ack = *mqtt_packet_ack(UNSUBACK_BYTE, pkt->unsubscribe.pkt_id);
    unsigned char *packed = pack_mqtt_packet(&ack, UNSUBACK);
    cb->payload = bytestring_create(MQTT_ACK_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
    delete[] packed;
    
    sol_debug("Sending UNSUBACK to %s", c->client_id.c_str());
    return REARM_W;
}

static int publish_handler(struct closure *cb, union mqtt_packet *pkt) {
    struct sol_client *c = static_cast<struct sol_client *>(cb->obj);
    sol_debug("Received PUBLISH from %s (d%i, q%u, r%i, m%u, %s, ... (%i bytes))",
              c->client_id.c_str(),
              pkt->publish.header.bits.dup,
              pkt->publish.header.bits.qos,
              pkt->publish.header.bits.retain,
              pkt->publish.pkt_id,
              pkt->publish.topic,
              pkt->publish.payloadlen);
    info.messages_recv++;

    std::string topic_str = reinterpret_cast<char*>(pkt->publish.topic);
    unsigned char qos = pkt->publish.header.bits.qos;

    if (!topic_str.empty() && topic_str.back() != '/') {
        topic_str += "/";
    }

    struct topic *t = sol_topic_get(&sol, topic_str.c_str());
    if (!t) {
        t = topic_create(topic_str);
        sol_topic_put(&sol, t);
    }

    for (const auto &sub : t->subscribers) {
        size_t publen = MQTT_HEADER_LEN + sizeof(uint16_t) + pkt->publish.topiclen + pkt->publish.payloadlen;
        struct sol_client *sc = sub.client;

        pkt->publish.header.bits.qos = sub.qos;
        if (pkt->publish.header.bits.qos > AT_MOST_ONCE)
            publen += sizeof(uint16_t);
            
        int remaininglen_offset = 0;
        if ((publen - 1) > 0x200000) remaininglen_offset = 3;
        else if ((publen - 1) > 0x4000) remaininglen_offset = 2;
        else if ((publen - 1) > 0x80) remaininglen_offset = 1;
        publen += remaininglen_offset;
        
        unsigned char *pub = pack_mqtt_packet(pkt, PUBLISH);
        ssize_t sent;
        if ((sent = send_bytes(sc->fd, pub, publen)) < 0) {
            sol_error("Error publishing to %s: %s", sc->client_id.c_str(), strerror(errno));
        }

        info.bytes_sent += sent;
        sol_debug("Sending PUBLISH to %s (d%i, q%u, r%i, m%u, %s, ... (%i bytes))",
                  sc->client_id.c_str(),
                  pkt->publish.header.bits.dup,
                  pkt->publish.header.bits.qos,
                  pkt->publish.header.bits.retain,
                  pkt->publish.pkt_id,
                  pkt->publish.topic,
                  pkt->publish.payloadlen);
        info.messages_sent++;
        delete[] pub;
    }

    if (qos == AT_LEAST_ONCE) {
        union mqtt_packet ack;
        ack.ack = *mqtt_packet_ack(PUBACK_BYTE, pkt->publish.pkt_id);
        unsigned char *packed = pack_mqtt_packet(&ack, PUBACK);
        cb->payload = bytestring_create(MQTT_ACK_LEN);
        std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
        delete[] packed;
        sol_debug("Sending PUBACK to %s", c->client_id.c_str());
        return REARM_W;
    } else if (qos == EXACTLY_ONCE) {
        union mqtt_packet ack;
        ack.ack = *mqtt_packet_ack(PUBREC_BYTE, pkt->publish.pkt_id);
        unsigned char *packed = pack_mqtt_packet(&ack, PUBREC);
        cb->payload = bytestring_create(MQTT_ACK_LEN);
        std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
        delete[] packed;
        sol_debug("Sending PUBREC to %s", c->client_id.c_str());
        return REARM_W;
    }

    return REARM_R;
}

static int puback_handler(struct closure *cb, union mqtt_packet *pkt) {
    sol_debug("Received PUBACK from %s", static_cast<struct sol_client *>(cb->obj)->client_id.c_str());
    return REARM_R;
}

static int pubrec_handler(struct closure *cb, union mqtt_packet *pkt) {
    struct sol_client *c = static_cast<struct sol_client *>(cb->obj);
    sol_debug("Received PUBREC from %s", c->client_id.c_str());
    
    union mqtt_packet ack;
    ack.ack = *mqtt_packet_ack(PUBREL_BYTE, pkt->publish.pkt_id);
    unsigned char *packed = pack_mqtt_packet(&ack, PUBREC);
    cb->payload = bytestring_create(MQTT_ACK_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
    delete[] packed;
    sol_debug("Sending PUBREL to %s", c->client_id.c_str());
    return REARM_W;
}

static int pubrel_handler(struct closure *cb, union mqtt_packet *pkt) {
    struct sol_client *c = static_cast<struct sol_client *>(cb->obj);
    sol_debug("Received PUBREL from %s", c->client_id.c_str());
    
    union mqtt_packet ack;
    ack.ack = *mqtt_packet_ack(PUBCOMP_BYTE, pkt->publish.pkt_id);
    unsigned char *packed = pack_mqtt_packet(&ack, PUBCOMP);
    cb->payload = bytestring_create(MQTT_ACK_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
    delete[] packed;
    sol_debug("Sending PUBCOMP to %s", c->client_id.c_str());
    return REARM_W;
}

static int pubcomp_handler(struct closure *cb, union mqtt_packet *pkt) {
    sol_debug("Received PUBCOMP from %s", static_cast<struct sol_client *>(cb->obj)->client_id.c_str());
    return REARM_R;
}

static int pingreq_handler(struct closure *cb, union mqtt_packet *pkt) {
    sol_debug("Received PINGREQ from %s", static_cast<struct sol_client *>(cb->obj)->client_id.c_str());
    union mqtt_packet pingresp;
    pingresp.header = *mqtt_packet_header(PINGRESP_BYTE);
    uint8_t *packed = pack_mqtt_packet(&pingresp, PINGRESP);
    cb->payload = bytestring_create(MQTT_HEADER_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_HEADER_LEN);
    delete[] packed;
    sol_debug("Sending PINGRESP to %s", static_cast<struct sol_client *>(cb->obj)->client_id.c_str());
    return REARM_W;
}

static int stub_handler(struct closure *cb, union mqtt_packet *pkt) {
    return REARM_R;
}

static handler *handlers[15] = {
    nullptr,
    connect_handler,
    nullptr,
    publish_handler,
    stub_handler, // puback
    stub_handler, // pubrec
    stub_handler, // pubrel
    stub_handler, // pubcomp
    subscribe_handler,
    nullptr,
    unsubscribe_handler,
    nullptr,
    pingreq_handler,
    nullptr,
    disconnect_handler
};

struct connection {
    char ip[INET_ADDRSTRLEN + 1];
    int fd;
};

static int accept_new_client(int fd, struct connection *conn) {
    if (!conn) return -1;
    int clientsock = accept_connection(fd, conf->socket_family);
    if (clientsock == -1) return -1;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(clientsock, reinterpret_cast<struct sockaddr *>(&addr), &addrlen) < 0)
        return -1;
    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == nullptr)
        return -1;
    
    conn->fd = clientsock;
    std::strcpy(conn->ip, ip_buff);
    return 0;
}

static void on_accept(struct evloop *loop, void *arg) {
    struct closure *server = static_cast<struct closure *>(arg);
    struct connection conn;
    if (accept_new_client(server->fd, &conn) < 0) return;

    struct closure *client_closure = new struct closure;
    client_closure->fd = conn.fd;
    client_closure->obj = nullptr;
    client_closure->payload = nullptr;
    client_closure->args = client_closure;
    client_closure->call = on_read;
    generate_uuid(client_closure->closure_id);

    sol.closures[client_closure->closure_id] = client_closure;
    evloop_add_callback(loop, client_closure);
    evloop_rearm_callback_read(loop, server);

    info.nclients++;
    info.nconnections++;
    sol_info("New connection from %s on port %s", conn.ip, conf->port);
}

static ssize_t recv_packet(int clientfd, uint8_t *buf, char *command) {
    ssize_t nbytes = 0;
    if ((nbytes = recv_bytes(clientfd, buf, 1)) <= 0)
        return -ERRCLIENTDC;
    
    uint8_t byte = *buf;
    buf++;
    if (DISCONNECT < byte || CONNECT > byte)
        return -ERRPACKETERR;

    uint8_t buff[4];
    int count = 0;
    ssize_t n = 0;
    do {
        if ((n = recv_bytes(clientfd, buf + count, 1)) <= 0)
            return -ERRCLIENTDC;
        buff[count] = buf[count];
        nbytes += n;
    } while (buff[count++] & (1 << 7));

    const uint8_t *pbuf = &buff[0];
    unsigned long long tlen = mqtt_decode_length(&pbuf);

    if (tlen > conf->max_request_size) {
        nbytes = -ERRMAXREQSIZE;
        goto err;
    }

    if ((n = recv_bytes(clientfd, buf + count, tlen)) < 0)
        goto err;
    nbytes += n;
    *command = byte;
    return nbytes;

err:
    shutdown(clientfd, SHUT_RDWR);
    close(clientfd);
    return nbytes;
}

static void on_read(struct evloop *loop, void *arg) {
    struct closure *cb = static_cast<struct closure *>(arg);
    uint8_t *buffer = new uint8_t[conf->max_request_size];
    char command = 0;
    union mqtt_header hdr;

    ssize_t bytes = recv_packet(cb->fd, buffer, &command);

    if (bytes == -ERRCLIENTDC || bytes == -ERRMAXREQSIZE)
        goto errdc;
    if (bytes == -ERRPACKETERR)
        goto errdc;
        
    info.bytes_recv += bytes;

    union mqtt_packet packet;
    if (unpack_mqtt_packet(buffer, &packet) != 0) goto errdc;
    hdr.byte = static_cast<uint8_t>(command);

    if (handlers[hdr.bits.type]) {
        int rc = handlers[hdr.bits.type](cb, &packet);
        if (rc == REARM_W) {
            cb->call = on_write;
            evloop_rearm_callback_write(loop, cb);
        } else if (rc == REARM_R) {
            cb->call = on_read;
            evloop_rearm_callback_read(loop, cb);
        } else if (rc == REARM_NONE) {
            goto errdc; // e.g. normal DISCONNECT
        }
    }

    mqtt_packet_release(&packet, hdr.bits.type);
    delete[] buffer;
    return;

errdc:
    delete[] buffer;
    sol_error("Dropping client");
    shutdown(cb->fd, SHUT_RDWR);
    close(cb->fd);
    
    if (cb->obj) {
        struct sol_client *client = static_cast<struct sol_client *>(cb->obj);
        sol.clients.erase(client->client_id);
        delete client;
    }
    
    std::string cid = cb->closure_id;
    sol.closures.erase(cid);
    delete cb;
    
    info.nclients--;
    info.nconnections--;
}

static void on_write(struct evloop *loop, void *arg) {
    struct closure *cb = static_cast<struct closure *>(arg);
    ssize_t sent;
    if ((sent = send_bytes(cb->fd, cb->payload->data, cb->payload->size)) < 0) {
        struct sol_client *sc = static_cast<struct sol_client *>(cb->obj);
        sol_error("Error writing on socket to client %s: %s",
                  sc ? sc->client_id.c_str() : "unknown", strerror(errno));
    }

    info.bytes_sent += sent;
    bytestring_release(cb->payload);
    cb->payload = nullptr;

    cb->call = on_read;
    evloop_rearm_callback_read(loop, cb);
}

static void publish_stats(struct evloop *loop, void *args) {
    sol_debug("Publish stats tick");
    // Part 4 full implementation
}

int start_server(const char *addr, const char *port) {
    trie_init(&sol.topics_trie);

    struct closure *server_closure = new struct closure;
    server_closure->fd = make_listen(addr, port, conf->socket_family);
    server_closure->payload = nullptr;
    server_closure->args = server_closure;
    server_closure->call = on_accept;
    generate_uuid(server_closure->closure_id);

    struct evloop *event_loop = evloop_create(256, 100); // Poll timeout 100ms
    evloop_add_callback(event_loop, server_closure);

    struct closure *sys_closure = new struct closure;
    sys_closure->fd = 0;
    sys_closure->payload = nullptr;
    sys_closure->args = sys_closure;
    sys_closure->call = publish_stats;
    generate_uuid(sys_closure->closure_id);

    evloop_add_periodic_task(event_loop, conf->stats_pub_interval, 0, sys_closure);

    sol_info("Server start");
    info.start_time = time(nullptr);
    
    if (evloop_wait(event_loop) < 0) {
        sol_error("Event loop exited unexpectedly");
    }
    
    evloop_free(event_loop);
    sol_info("Sol exiting");
    return 0;
}
