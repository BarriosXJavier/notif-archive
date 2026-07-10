#ifndef PARSER_H
#define PARSER_H

// Result of splitting a raw D-Bus Notify() summary/body pair into
// sender/content, and (for group chats) the group name. Fixed-size
// buffers embedded directly in the struct -- no malloc/free needed,
// the whole thing lives on the stack in bus_listener.c.
typedef struct {
    char group[256];      // empty/unused if not a group message
    char sender[128];
    char content[2048];
    int is_group;         // 1 if this came from a group chat, 0 for 1:1
} parsed_message_t;

// Fills *out based on summary/body as received from Notify(). Handles
// both cases:
//   1:1 chat  -> summary is the sender, body is the message text
//   group chat -> summary is the group name, body is "Sender: message"
void parser_split(const char *summary, const char *body, parsed_message_t *out);

#endif
