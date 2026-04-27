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
    struct sol_client *client = new struct sol_client;
    client->fd = cb->fd;
    client->client_id = reinterpret_cast<char*>(pkt->connect.payload.client_id);
    client->will_topic = nullptr;
    client->will_message = nullptr;

    if (pkt->connect.bits.will == 1) {
        // Will store will message for part 4
    }

    cb->obj = client;
    sol.clients[client->client_id] = client;

    sol_info("Client %s connected", client->client_id.c_str());

    union mqtt_packet connack;
    connack.connack = *mqtt_packet_connack(CONNACK_BYTE, 0, 0); // 0 = Accepted
    uint8_t *packed = pack_mqtt_packet(&connack, CONNACK);

    cb->payload = bytestring_create(MQTT_ACK_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_ACK_LEN);
    delete[] packed;

    return REARM_W;
}

static int disconnect_handler(struct closure *cb, union mqtt_packet *pkt) {
    sol_debug("Client disconnected normally");
    return REARM_NONE; // Will close socket in on_read
}

static int publish_handler(struct closure *cb, union mqtt_packet *pkt) {
    sol_debug("Publish handler called");
    // Part 4 implementation
    return REARM_R;
}

static int subscribe_handler(struct closure *cb, union mqtt_packet *pkt) {
    sol_debug("Subscribe handler called");
    // Part 4 implementation
    union mqtt_packet suback;
    uint8_t rc = 0; // Success QOS 0
    suback.suback = *mqtt_packet_suback(SUBACK_BYTE, pkt->subscribe.pkt_id, &rc, 1);
    uint8_t *packed = pack_mqtt_packet(&suback, SUBACK);
    
    cb->payload = bytestring_create(MQTT_HEADER_LEN + sizeof(uint16_t) + 1);
    std::memcpy(cb->payload->data, packed, MQTT_HEADER_LEN + sizeof(uint16_t) + 1);
    delete[] packed;
    
    // Clean up allocated suback
    delete[] suback.suback.rcs;
    delete &suback.suback;

    return REARM_W;
}

static int unsubscribe_handler(struct closure *cb, union mqtt_packet *pkt) {
    return REARM_R;
}

static int pingreq_handler(struct closure *cb, union mqtt_packet *pkt) {
    union mqtt_packet pingresp;
    pingresp.header = *mqtt_packet_header(PINGRESP_BYTE);
    uint8_t *packed = pack_mqtt_packet(&pingresp, PINGRESP);

    cb->payload = bytestring_create(MQTT_HEADER_LEN);
    std::memcpy(cb->payload->data, packed, MQTT_HEADER_LEN);
    delete[] packed;

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
