#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>

/*
 * Socket family options (mirrors the network module constants).
 */
#define UNIX_SOCK 0
#define INET      1

/*
 * Global broker configuration.  A single instance is heap-allocated by
 * config_new() / config_from_file() and exposed via the 'conf' extern
 * pointer so that every module can read it without circular includes.
 */
struct config {
    /* Listening address and port (as strings) */
    char addr[64];
    char port[8];

    /* Log level: 0=DEBUG 1=INFORMATION 2=WARNING 3=ERROR */
    int loglevel;

    /* Path to the log file; empty string disables file logging */
    char logfile[256];

    /* AF_INET or UNIX domain socket */
    int socket_family;

    /*
     * Maximum allowed size of a single incoming MQTT packet in bytes.
     * Packets larger than this are dropped.  Default: 2 MB.
     */
    size_t max_request_size;

    /*
     * Interval (seconds) at which broker statistics are published to the
     * $SOL/# topic tree.
     */
    int stats_pub_interval;
};

/* Global configuration pointer, defined in config.cpp */
extern struct config *conf;

/* Allocate a config struct with sensible defaults */
struct config *config_new(const char *addr, const char *port);

/*
 * (Optional) Load overrides from a simple INI-style file.
 * Lines of the form  key = value  are supported.
 * Unrecognised keys are silently ignored.
 */
void config_from_file(struct config *, const char *path);

/* Release memory */
void config_free(struct config *);

#endif // CONFIG_HPP
