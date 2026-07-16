#include "parser.h"

#include <stddef.h>
#include <string.h>

static size_t valid_utf8_sequence(const unsigned char *s) {
    if (s[0] < 0x80)
        return 1;
    if (s[0] >= 0xc2 && s[0] <= 0xdf && s[1] >= 0x80 && s[1] <= 0xbf)
        return 2;
    if (s[0] == 0xe0 && s[1] >= 0xa0 && s[1] <= 0xbf &&
        s[2] >= 0x80 && s[2] <= 0xbf)
        return 3;
    if (((s[0] >= 0xe1 && s[0] <= 0xec) ||
         (s[0] >= 0xee && s[0] <= 0xef)) &&
        s[1] >= 0x80 && s[1] <= 0xbf && s[2] >= 0x80 && s[2] <= 0xbf)
        return 3;
    if (s[0] == 0xed && s[1] >= 0x80 && s[1] <= 0x9f &&
        s[2] >= 0x80 && s[2] <= 0xbf)
        return 3;
    if (s[0] == 0xf0 && s[1] >= 0x90 && s[1] <= 0xbf &&
        s[2] >= 0x80 && s[2] <= 0xbf && s[3] >= 0x80 && s[3] <= 0xbf)
        return 4;
    if (s[0] >= 0xf1 && s[0] <= 0xf3 && s[1] >= 0x80 && s[1] <= 0xbf &&
        s[2] >= 0x80 && s[2] <= 0xbf && s[3] >= 0x80 && s[3] <= 0xbf)
        return 4;
    if (s[0] == 0xf4 && s[1] >= 0x80 && s[1] <= 0x8f &&
        s[2] >= 0x80 && s[2] <= 0xbf && s[3] >= 0x80 && s[3] <= 0xbf)
        return 4;
    return 0;
}

static void utf8_copy(char *out, size_t out_sz, const char *in) {
    size_t i = 0;
    size_t j = 0;

    if (out_sz == 0)
        return;
    if (!in) {
        out[0] = '\0';
        return;
    }

    while (in[i] != '\0') {
        const unsigned char *p = (const unsigned char *)in + i;
        size_t sequence_len = valid_utf8_sequence(p);

        if (sequence_len == 0) {
            if (j + 1 >= out_sz)
                break;
            out[j++] = '?';
            i++;
            continue;
        }
        if (j + sequence_len >= out_sz)
            break;
        memcpy(out + j, in + i, sequence_len);
        i += sequence_len;
        j += sequence_len;
    }
    out[j] = '\0';
}

static int split_group_body(const char *body, char *sender_out, size_t sender_sz,
                            char *content_out, size_t content_sz) {
    const char *colon;
    size_t name_len;

    if (!body)
        return 0;
    colon = strstr(body, ": ");
    if (!colon || colon == body)
        return 0;

    name_len = (size_t)(colon - body);
    if (name_len >= sender_sz || name_len > 50)
        return 0;

    {
        char sender_prefix[51];
        memcpy(sender_prefix, body, name_len);
        sender_prefix[name_len] = '\0';
        utf8_copy(sender_out, sender_sz, sender_prefix);
    }
    utf8_copy(content_out, content_sz, colon + 2);
    return 1;
}

void parser_split(const char *summary, const char *body, parsed_message_t *out) {
    memset(out, 0, sizeof(*out));

    if (split_group_body(body, out->sender, sizeof(out->sender), out->content,
                         sizeof(out->content))) {
        utf8_copy(out->group, sizeof(out->group), summary);
        out->is_group = 1;
    } else {
        utf8_copy(out->sender, sizeof(out->sender), summary);
        utf8_copy(out->content, sizeof(out->content), body);
        out->is_group = 0;
    }
}
