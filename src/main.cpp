#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "server.hpp"
#include "config.hpp"
#include "util.hpp"

int main(int argc, char **argv) {
    const char *addr = "127.0.0.1";
    const char *port = "1883";
    const char *confpath = nullptr;
    int debug = 0;
    int opt;

    conf = config_new(addr, port);

    while ((opt = getopt(argc, argv, "a:c:p:v")) != -1) {
        switch (opt) {
            case 'a':
                std::strncpy(conf->addr, optarg, sizeof(conf->addr) - 1);
                break;
            case 'c':
                confpath = optarg;
                break;
            case 'p':
                std::strncpy(conf->port, optarg, sizeof(conf->port) - 1);
                break;
            case 'v':
                debug = 1;
                break;
            default:
                std::fprintf(stderr, "Usage: %s [-a addr] [-p port] [-c conf] [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (debug) conf->loglevel = 0; // DEBUG level

    // Try to load a configuration, if found
    if (confpath) {
        config_from_file(conf, confpath);
    }

    sol_log_init(conf->logfile[0] ? conf->logfile : "");
    
    start_server(conf->addr, conf->port);

    sol_log_close();
    config_free(conf);
    return 0;
}
