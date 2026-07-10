#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_direct_message(void) {
    parsed_message_t msg;
    parser_split("Alice", "Hello there", &msg);

    assert(msg.is_group == 0);
    assert(strcmp(msg.group, "") == 0);
    assert(strcmp(msg.sender, "Alice") == 0);
    assert(strcmp(msg.content, "Hello there") == 0);
}

static void test_group_message(void) {
    parsed_message_t msg;
    parser_split("Family Group", "Bob: Dinner is ready", &msg);

    assert(msg.is_group == 1);
    assert(strcmp(msg.group, "Family Group") == 0);
    assert(strcmp(msg.sender, "Bob") == 0);
    assert(strcmp(msg.content, "Dinner is ready") == 0);
}

static void test_body_without_sender_prefix_stays_direct(void) {
    parsed_message_t msg;
    parser_split("Carol", "No delimiter here", &msg);

    assert(msg.is_group == 0);
    assert(strcmp(msg.group, "") == 0);
    assert(strcmp(msg.sender, "Carol") == 0);
    assert(strcmp(msg.content, "No delimiter here") == 0);
}

static void test_overlong_sender_prefix_stays_direct(void) {
    parsed_message_t msg;
    parser_split(
        "Busy Chat",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz: message",
        &msg);

    assert(msg.is_group == 0);
    assert(strcmp(msg.group, "") == 0);
    assert(strcmp(msg.sender, "Busy Chat") == 0);
    assert(strcmp(msg.content,
                  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz: message") == 0);
}

int main(void) {
    test_direct_message();
    test_group_message();
    test_body_without_sender_prefix_stays_direct();
    test_overlong_sender_prefix_stays_direct();

    printf("parser tests passed\n");
    return 0;
}
