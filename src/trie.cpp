#include "trie.hpp"
#include <sstream>

trie_node::~trie_node() {
    for (auto child : children)
        delete child;
}

trie::trie() : root(nullptr) {}
trie::~trie() { delete root; }

void trie_init(struct trie *t) {
    if (!t->root)
        t->root = new trie_node("");
}

static std::vector<std::string> split_topic(const std::string &topic_str) {
    std::vector<std::string> levels;
    std::stringstream ss(topic_str);
    std::string level;
    while (std::getline(ss, level, '/')) {
        levels.push_back(level);
    }
    /* Handle trailing slash, e.g. "a/b/" -> ["a", "b", ""] */
    if (!topic_str.empty() && topic_str.back() == '/')
        levels.push_back("");
    return levels;
}

void trie_insert(struct trie *t, const std::string &topic_str, struct topic *tp) {
    if (!t->root) trie_init(t);
    std::vector<std::string> levels = split_topic(topic_str);
    trie_node *curr = t->root;

    for (const auto &level : levels) {
        trie_node *next = nullptr;
        for (auto child : curr->children) {
            if (child->topic_level == level) {
                next = child;
                break;
            }
        }
        if (!next) {
            next = new trie_node(level);
            curr->children.push_back(next);
        }
        curr = next;
    }
    curr->topic_ptr = tp;
}

static void trie_find_recursive(trie_node *node, const std::vector<std::string> &levels, size_t depth, std::vector<struct topic *> &matches) {
    if (depth == levels.size()) {
        if (node->topic_ptr) matches.push_back(node->topic_ptr);
        /* Check if there's a child '#' that matches the end of the string */
        for (auto child : node->children) {
            if (child->topic_level == "#" && child->topic_ptr)
                matches.push_back(child->topic_ptr);
        }
        return;
    }

    const std::string &level = levels[depth];
    for (auto child : node->children) {
        if (child->topic_level == "#") {
            if (child->topic_ptr) matches.push_back(child->topic_ptr);
        } else if (child->topic_level == "+") {
            trie_find_recursive(child, levels, depth + 1, matches);
        } else if (child->topic_level == level) {
            trie_find_recursive(child, levels, depth + 1, matches);
        }
    }
}

void trie_find(struct trie *t, const std::string &topic_str, std::vector<struct topic *> &matches) {
    if (!t->root) return;
    std::vector<std::string> levels = split_topic(topic_str);
    trie_find_recursive(t->root, levels, 0, matches);
}

static void trie_prefix_map_func(struct trie_node *node, void (*mapfunc)(struct trie_node *, void *), void *arg) {
    if (!node) return;
    mapfunc(node, arg);
    for (auto child : node->children) {
        trie_prefix_map_func(child, mapfunc, arg);
    }
}

void trie_prefix_map_tuple(struct trie *t, const std::string &prefix,
                           void (*mapfunc)(struct trie_node *, void *), void *arg) {
    if (!t->root) return;
    
    if (prefix.empty()) {
        trie_prefix_map_func(t->root, mapfunc, arg);
        return;
    }
    
    std::vector<std::string> levels = split_topic(prefix);
    struct trie_node *node = t->root;
    for (const auto &level : levels) {
        struct trie_node *next = nullptr;
        for (auto child : node->children) {
            if (child->topic_level == level) {
                next = child;
                break;
            }
        }
        if (!next) return; // Prefix not found
        node = next;
    }
    
    trie_prefix_map_func(node, mapfunc, arg);
}
