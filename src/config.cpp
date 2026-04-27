#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "config.hpp"
#include "util.hpp"

/* The one global config pointer, initialised by config_new() in main */
struct config *conf = nullptr;

struct config *config_new(const char *addr, const char *port) {
    struct config *c = new struct config;
    std::strncpy(c->addr, addr, sizeof(c->addr) - 1);
    c->addr[sizeof(c->addr) - 1] = '\0';
    std::strncpy(c->port, port, sizeof(c->port) - 1);
    c->port[sizeof(c->port) - 1] = '\0';
    c->loglevel           = INFORMATION;
    c->logfile[0]         = '\0';
    c->socket_family      = INET;
    c->max_request_size   = 2UL * 1024 * 1024; /* 2 MB */
    c->stats_pub_interval = 10;                 /* seconds */
    return c;
}

void config_from_file(struct config *c, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64], val[256];
        if (sscanf(line, " %63[^=] = %255[^\n]", key, val) != 2) continue;
        /* Trim trailing whitespace from key */
        char *kend = key + std::strlen(key) - 1;
        while (kend > key && *kend == ' ') *kend-- = '\0';

        if      (std::strcmp(key, "addr")             == 0)
            std::strncpy(c->addr, val, sizeof(c->addr) - 1);
        else if (std::strcmp(key, "port")             == 0)
            std::strncpy(c->port, val, sizeof(c->port) - 1);
        else if (std::strcmp(key, "loglevel")         == 0)
            c->loglevel = parse_int(val);
        else if (std::strcmp(key, "logfile")          == 0)
            std::strncpy(c->logfile, val, sizeof(c->logfile) - 1);
        else if (std::strcmp(key, "socket_family")    == 0)
            c->socket_family = (std::strcmp(val, "unix") == 0) ? UNIX_SOCK : INET;
        else if (std::strcmp(key, "max_request_size") == 0)
            c->max_request_size = (size_t) parse_int(val);
        else if (std::strcmp(key, "stats_pub_interval") == 0)
            c->stats_pub_interval = parse_int(val);
    }
    fclose(fp);
}

void config_free(struct config *c) {
    delete c;
}
