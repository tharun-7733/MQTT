#ifndef TRIE_HPP
#define TRIE_HPP

#include <string>
#include <vector>
#include "server.hpp"

/* Node in the topic trie */
struct trie_node {
    std::string topic_level;
    struct topic *topic_ptr; /* non-null if this is an exact subscribed topic */
    std::vector<struct trie_node *> children;

    trie_node(const std::string &level) : topic_level(level), topic_ptr(nullptr) {}
    ~trie_node();
};

/* The Trie structure */
struct trie {
    struct trie_node *root;

    trie();
    ~trie();
};

/* API */
void trie_init(struct trie *t);
void trie_insert(struct trie *t, const std::string &topic_str, struct topic *tp);
/* Find all topics matching a given publish topic string (handles + and # wildcards) */
void trie_find(struct trie *t, const std::string &topic_str, std::vector<struct topic *> &matches);

/* Map a function to all nodes matching a given prefix */
void trie_prefix_map_tuple(struct trie *t, const std::string &prefix,
                           void (*mapfunc)(struct trie_node *, void *), void *arg);

#endif // TRIE_HPP
