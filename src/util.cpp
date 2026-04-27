#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <ctime>
#include <cstdarg>
#include <cctype>
#include <strings.h>
#include <uuid/uuid.h>
#include "util.hpp"
#include "config.hpp"

static FILE *fh = nullptr;

void sol_log_init(const char *file) {
    if (!file || file[0] == '\0') return;
    fh = fopen(file, "a+");
    if (!fh)
        printf("%lu * WARNING: Unable to open log file %s\n",
               (unsigned long) time(nullptr), file);
}

void sol_log_close(void) {
    if (fh) {
        fflush(fh);
        fclose(fh);
        fh = nullptr;
    }
}

void sol_log(int level, const char *fmt, ...) {
    assert(fmt);
    /* Suppress messages below configured log level */
    if (conf && level < conf->loglevel) return;

    va_list ap;
    char msg[MAX_LOG_SIZE + 4];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Truncate long messages and append "..." */
    std::memcpy(msg + MAX_LOG_SIZE, "...", 3);
    msg[MAX_LOG_SIZE + 3] = '\0';

    /* Level prefix characters: # i * ! */
    const char *mark = "#i*!";

    FILE *fp = stdout;
    if (!fp) return;

    fprintf(fp, "%lu %c %s\n", (unsigned long) time(nullptr), mark[level], msg);
    if (fh)
        fprintf(fh, "%lu %c %s\n", (unsigned long) time(nullptr), mark[level], msg);
    fflush(fp);
    if (fh) fflush(fh);
}

int number_len(size_t number) {
    int len = 1;
    while (number) { len++; number /= 10; }
    return len;
}

int parse_int(const char *string) {
    int n = 0;
    while (*string && isdigit((unsigned char) *string)) {
        n = (n * 10) + (*string - '0');
        string++;
    }
    return n;
}

char *remove_occur(char *str, char c) {
    char *p = str, *pp = str;
    while (*p) { *pp = *p++; pp += (*pp != c); }
    *pp = '\0';
    return str;
}

char *append_string(char *src, char *chunk, size_t chunklen) {
    size_t srclen = std::strlen(src);
    char *ret = new char[srclen + chunklen + 1];
    std::memcpy(ret, src, srclen);
    std::memcpy(ret + srclen, chunk, chunklen);
    ret[srclen + chunklen] = '\0';
    return ret;
}

int generate_uuid(char *uuid_placeholder) {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    uuid_unparse(binuuid, uuid_placeholder);
    return 0;
}
