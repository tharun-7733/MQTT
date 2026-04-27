#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/event.h>   // kqueue (macOS equivalent of epoll)
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "network.hpp"

/******************************
 *       SOCKET HELPERS       *
 ******************************/

/* Set non-blocking socket */
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("set_nonblocking: fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("set_nonblocking: fcntl F_SETFL");
        return -1;
    }
    return 0;
}

/* Disable Nagle's algorithm by setting TCP_NODELAY */
int set_tcp_nodelay(int fd) {
    int flag = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}

static int create_and_bind_unix(const char *sockpath) {
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("create_and_bind_unix: socket");
        return -1;
    }
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockpath, sizeof(addr.sun_path) - 1);
    unlink(sockpath);
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        perror("create_and_bind_unix: bind");
        close(fd);
        return -1;
    }
    return fd;
}

static int create_and_bind_tcp(const char *host, const char *port) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    struct addrinfo *result = nullptr;
    if (getaddrinfo(host, port, &hints, &result) != 0) {
        perror("create_and_bind_tcp: getaddrinfo");
        return -1;
    }

    int sfd = -1;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
        int opt = 1;
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0)
            perror("create_and_bind_tcp: SO_REUSEADDR");
        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // success
        close(sfd);
        sfd = -1;
    }
    freeaddrinfo(result);
    if (sfd == -1)
        perror("create_and_bind_tcp: could not bind");
    return sfd;
}

int create_and_bind(const char *host, const char *port, int socket_family) {
    if (socket_family == UNIX_SOCK)
        return create_and_bind_unix(host);
    return create_and_bind_tcp(host, port);
}

/*
 * Create a non-blocking socket, bind it, and start listening.
 */
int make_listen(const char *host, const char *port, int socket_family) {
    int sfd = create_and_bind(host, port, socket_family);
    if (sfd == -1) abort();
    if (set_nonblocking(sfd) == -1) abort();
    if (socket_family == INET)
        set_tcp_nodelay(sfd);
    static const int BACKLOG = 128;
    if (listen(sfd, BACKLOG) == -1) {
        perror("make_listen: listen");
        abort();
    }
    return sfd;
}

int accept_connection(int serversock, int socket_family) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int clientsock = accept(serversock,
                            reinterpret_cast<struct sockaddr *>(&addr),
                            &addrlen);
    if (clientsock < 0)
        return -1;
    set_nonblocking(clientsock);
    if (socket_family == INET)
        set_tcp_nodelay(clientsock);
    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == nullptr) {
        close(clientsock);
        return -1;
    }
    return clientsock;
}

/******************************
 *        I/O FUNCTIONS       *
 ******************************/

/* Send all bytes in buf, handling EAGAIN / EWOULDBLOCK */
ssize_t send_bytes(int fd, const uint8_t *buf, size_t len) {
    size_t total = 0;
    size_t bytesleft = len;
    ssize_t n = 0;
    while (total < len) {
        n = send(fd, buf + total, bytesleft, MSG_NOSIGNAL);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            std::fprintf(stderr, "send_bytes: %s\n", std::strerror(errno));
            return -1;
        }
        total     += n;
        bytesleft -= n;
    }
    return static_cast<ssize_t>(total);
}

/* Receive up to bufsize bytes from fd, handling EAGAIN / EWOULDBLOCK */
ssize_t recv_bytes(int fd, uint8_t *buf, size_t bufsize) {
    ssize_t total = 0;
    while (total < static_cast<ssize_t>(bufsize)) {
        ssize_t n = recv(fd, buf + total, bufsize - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            std::fprintf(stderr, "recv_bytes: %s\n", std::strerror(errno));
            return -1;
        }
        if (n == 0) return 0; // peer closed connection
        total += n;
    }
    return total;
}

/******************************
 *     KQUEUE EVENT LOOP      *
 ******************************/

#define EVLOOP_INITIAL_SIZE 4

/*
 * Low-level kqueue helpers.
 * These mirror the blog's epoll_add / epoll_mod / epoll_del.
 * EV_ONESHOT disables the event after it fires — it must be re-armed.
 */
int kqueue_add_read(int kqfd, int fd, void *data) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, data);
    return kevent(kqfd, &ev, 1, nullptr, 0, nullptr);
}

int kqueue_mod_read(int kqfd, int fd, void *data) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, data);
    return kevent(kqfd, &ev, 1, nullptr, 0, nullptr);
}

int kqueue_mod_write(int kqfd, int fd, void *data) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, data);
    return kevent(kqfd, &ev, 1, nullptr, 0, nullptr);
}

int kqueue_del(int kqfd, int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    int r = kevent(kqfd, &ev, 1, nullptr, 0, nullptr);
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqfd, &ev, 1, nullptr, 0, nullptr); // ignore error if not registered
    return r;
}

/* Create and initialise an evloop */
struct evloop *evloop_create(int max_events, int timeout) {
    struct evloop *loop = new struct evloop;
    evloop_init(loop, max_events, timeout);
    return loop;
}

void evloop_init(struct evloop *loop, int max_events, int timeout) {
    loop->max_events        = max_events;
    loop->kqfd              = kqueue();
    loop->timeout           = timeout;
    loop->status            = 0;
    loop->periodic_maxsize  = EVLOOP_INITIAL_SIZE;
    loop->periodic_nr       = 0;
    loop->periodic_tasks    =
        new evloop::periodic_task *[EVLOOP_INITIAL_SIZE];
}

void evloop_free(struct evloop *loop) {
    close(loop->kqfd);
    for (int i = 0; i < loop->periodic_nr; i++)
        delete loop->periodic_tasks[i];
    delete[] loop->periodic_tasks;
    delete loop;
}

/* Register a closure to be monitored for read events (EPOLLIN equivalent) */
void evloop_add_callback(struct evloop *loop, struct closure *cb) {
    if (kqueue_add_read(loop->kqfd, cb->fd, cb) < 0)
        perror("evloop_add_callback: kqueue_add_read");
}

/*
 * Register a periodic task using kqueue's EVFILT_TIMER.
 * The timer fires every `seconds * 1000 + ns / 1_000_000` milliseconds.
 * This replaces the blog's timerfd_create approach which is Linux-only.
 */
void evloop_add_periodic_task(struct evloop *loop,
                              int seconds,
                              unsigned long long ns,
                              struct closure *cb) {
    // kqueue EVFILT_TIMER ident must be unique; use periodic_nr as id
    uintptr_t timer_id = static_cast<uintptr_t>(loop->periodic_nr + 100000);
    intptr_t interval_ms = static_cast<intptr_t>(seconds * 1000 + ns / 1000000ULL);

    struct kevent ev;
    EV_SET(&ev, timer_id, EVFILT_TIMER, EV_ADD, 0, interval_ms, cb);
    if (kevent(loop->kqfd, &ev, 1, nullptr, 0, nullptr) < 0) {
        perror("evloop_add_periodic_task: kevent EVFILT_TIMER");
        return;
    }

    // Grow the periodic_tasks array if needed
    if (loop->periodic_nr + 1 > loop->periodic_maxsize) {
        loop->periodic_maxsize *= 2;
        auto *tmp = new evloop::periodic_task *[loop->periodic_maxsize];
        for (int i = 0; i < loop->periodic_nr; i++)
            tmp[i] = loop->periodic_tasks[i];
        delete[] loop->periodic_tasks;
        loop->periodic_tasks = tmp;
    }

    loop->periodic_tasks[loop->periodic_nr] =
        new evloop::periodic_task;
    loop->periodic_tasks[loop->periodic_nr]->timer_id  = timer_id;
    loop->periodic_tasks[loop->periodic_nr]->closure   = cb;
    loop->periodic_nr++;
}

/*
 * Main event loop — blocks waiting for events and dispatches callbacks.
 * Mirrors evloop_wait from the blog but uses kevent() instead of epoll_wait().
 */
int evloop_wait(struct evloop *el) {
    int rc = 0;
    struct kevent *events = new struct kevent[el->max_events];

    while (1) {
        struct timespec ts;
        ts.tv_sec  = el->timeout / 1000;
        ts.tv_nsec = (el->timeout % 1000) * 1000000L;

        int nev = kevent(el->kqfd, nullptr, 0, events, el->max_events, &ts);
        if (nev < 0) {
            if (errno == EINTR) continue; // interrupted by signal, keep going
            perror("evloop_wait: kevent");
            rc = -1;
            el->status = errno;
            break;
        }

        for (int i = 0; i < nev; i++) {
            // Check for errors on the descriptor
            if (events[i].flags & EV_ERROR) {
                std::fprintf(stderr, "evloop_wait: EV_ERROR on fd %lu: %s\n",
                             events[i].ident, std::strerror(events[i].data));
                shutdown(static_cast<int>(events[i].ident), SHUT_RDWR);
                close(static_cast<int>(events[i].ident));
                el->status = static_cast<int>(events[i].data);
                continue;
            }

            // Check if this event is a periodic timer (EVFILT_TIMER)
            if (events[i].filter == EVFILT_TIMER) {
                struct closure *c = static_cast<struct closure *>(events[i].udata);
                if (c && c->call)
                    c->call(el, c->args);
                continue;
            }

            // Normal read / write event — dispatch the registered closure
            struct closure *closure = static_cast<struct closure *>(events[i].udata);
            if (closure && closure->call)
                closure->call(el, closure->args);
        }
    }

    delete[] events;
    return rc;
}

/* Re-arm a closure for read events */
int evloop_rearm_callback_read(struct evloop *el, struct closure *cb) {
    return kqueue_mod_read(el->kqfd, cb->fd, cb);
}

/* Re-arm a closure for write events */
int evloop_rearm_callback_write(struct evloop *el, struct closure *cb) {
    return kqueue_mod_write(el->kqfd, cb->fd, cb);
}

/* Remove a closure from the event loop */
int evloop_del_callback(struct evloop *el, struct closure *cb) {
    return kqueue_del(el->kqfd, cb->fd);
}
