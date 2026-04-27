#include <cstdio>
#include <cstdlib>
#include "server.hpp"
#include "config.hpp"
#include "util.hpp"

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <address> <port>\n", argv[0]);
        return 1;
    }

    const char *addr = argv[1];
    const char *port = argv[2];

    conf = config_new(addr, port);
    
    // Default to logging to console, simple file if provided
    sol_log_init(conf->logfile[0] ? conf->logfile : "");

    start_server(addr, port);

    sol_log_close();
    config_free(conf);
    return 0;
}
