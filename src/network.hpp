#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <cstdio>
#include <cstdint>
#include <sys/types.h>
#include "util.hpp"

// Socket families
#define UNIX_SOCK 0
#define INET      1

/* Set non-blocking socket */
int set_nonblocking(int);

/*
 * Set TCP_NODELAY flag to true, disabling Nagle's algorithm, no more waiting
 * for incoming packets on the buffer
 */
int set_tcp_nodelay(int);

/* Auxiliary function for creating and binding a server socket */
int create_and_bind(const char *, const char *, int);

/*
 * Create a non-blocking socket and make it listen on the specified address and
 * port
 */
int make_listen(const char *, const char *, int);

/* Accept a connection and return the new client socket fd */
int accept_connection(int, int);

/* I/O management functions */

/*
 * Send all data in a loop, avoiding interruption based on the kernel buffer
 * availability
 */
ssize_t send_bytes(int, const uint8_t *, size_t);

/*
 * Receive (read) an arbitrary number of bytes from a file descriptor and
 * store them in a buffer
 */
ssize_t recv_bytes(int, uint8_t *, size_t);

/* Forward declare closure so evloop can reference it */
struct closure;

/*
 * Event loop wrapper structure, wraps a kqueue instance and its state.
 * Uses EVFILT_READ / EVFILT_WRITE with EV_ONESHOT, equivalent to
 * epoll's EPOLLONESHOT, so each event must be manually re-armed.
 */
struct evloop {
    int kqfd;           // kqueue file descriptor (epollfd equivalent)
    int max_events;
    int timeout;        // milliseconds
    int status;
    /* Dynamic array of periodic tasks: a pair (virtual timerfd, closure) */
    int periodic_maxsize;
    int periodic_nr;
    struct periodic_task {
        uintptr_t timer_id;  // unique id used as kqueue EVFILT_TIMER ident
        struct closure *closure;
    } **periodic_tasks;
};

typedef void callback(struct evloop *, void *);

/*
 * Closure: pairs a file descriptor with a callback function and its arguments.
 * payload holds a serialized result ready to be sent over the wire.
 */
struct closure {
    int fd;
    void *obj;
    void *args;
    char closure_id[UUID_LEN];
    struct bytestring *payload;
    callback *call;
};

struct evloop *evloop_create(int, int);
void evloop_init(struct evloop *, int, int);
void evloop_free(struct evloop *);

/*
 * Blocks in a while(1) loop awaiting for events on monitored file descriptors
 * and executing the paired callback previously registered.
 */
int evloop_wait(struct evloop *);

/* Register a closure to be called on EPOLLIN-equivalent read events */
void evloop_add_callback(struct evloop *, struct closure *);

/*
 * Register a periodic closure to be executed every `seconds` seconds
 * (plus `ns` nanoseconds). Uses kqueue EVFILT_TIMER internally.
 */
void evloop_add_periodic_task(struct evloop *,
                              int,
                              unsigned long long,
                              struct closure *);

/* Unregister a closure by removing its descriptor from the event loop */
int evloop_del_callback(struct evloop *, struct closure *);

/* Rearm the fd for read events */
int evloop_rearm_callback_read(struct evloop *, struct closure *);

/* Rearm the fd for write events */
int evloop_rearm_callback_write(struct evloop *, struct closure *);

/* Low-level kqueue management helpers */
int kqueue_add_read(int kqfd, int fd, void *data);
int kqueue_mod_read(int kqfd, int fd, void *data);
int kqueue_mod_write(int kqfd, int fd, void *data);
int kqueue_del(int kqfd, int fd);

#endif // NETWORK_HPP
