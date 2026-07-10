#include "parser.h"
#include <string.h>
#include <stdio.h>

// Attempts to split body as "Sender: message text". Returns 1 and
// fills sender_out/content_out if it looks like a group-chat message;
// returns 0 (leaving the outputs untouched) otherwise.
static int split_group_body(const char *body, char *sender_out, size_t sender_sz,
                             char *content_out, size_t content_sz) {
    const char *colon = strstr(body, ": ");
    if (!colon || colon == body) return 0; // no ": " found, or it's at position 0

    size_t name_len = colon - body; // pointer arithmetic: chars before the colon
    if (name_len >= sender_sz || name_len > 50) return 0; // too long to be a real name

    memcpy(sender_out, body, name_len);
    sender_out[name_len] = '\0'; // memcpy doesn't null-terminate, do it manually

    snprintf(content_out, content_sz, "%s", colon + 2); // skip past ": "
    return 1;
}

void parser_split(const char *summary, const char *body, parsed_message_t *out) {
    memset(out, 0, sizeof(*out)); // guarantee clean state, no leftover stack garbage

    if (split_group_body(body, out->sender, sizeof(out->sender),
                          out->content, sizeof(out->content))) {
        // group chat: summary is the group name
        snprintf(out->group, sizeof(out->group), "%s", summary);
        out->is_group = 1;
    } else {
        // 1:1 chat: summary is the sender, body is the message as-is
        snprintf(out->sender, sizeof(out->sender), "%s", summary);
        snprintf(out->content, sizeof(out->content), "%s", body);
        out->is_group = 0;
    }
}
