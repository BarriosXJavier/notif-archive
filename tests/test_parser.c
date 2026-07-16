#include "../src/parser.h"

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

static void test_empty_fields(void) {
    parsed_message_t msg;
    parser_split("", "", &msg);

    assert(msg.is_group == 0);
    assert(msg.sender[0] == '\0');
    assert(msg.content[0] == '\0');
}

static void test_body_without_sender_prefix_stays_direct(void) {
    parsed_message_t msg;
    parser_split("Carol", "No delimiter here", &msg);

    assert(msg.is_group == 0);
    assert(strcmp(msg.sender, "Carol") == 0);
    assert(strcmp(msg.content, "No delimiter here") == 0);
}

static void test_overlong_sender_prefix_stays_direct(void) {
    parsed_message_t msg;
    parser_split(
        "Busy Chat",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz: message", &msg);

    assert(msg.is_group == 0);
    assert(strcmp(msg.sender, "Busy Chat") == 0);
}

static void test_unicode_is_preserved(void) {
    parsed_message_t msg;
    parser_split("Zoë 👩‍💻", "Привет 🌍", &msg);

    assert(strcmp(msg.sender, "Zoë 👩‍💻") == 0);
    assert(strcmp(msg.content, "Привет 🌍") == 0);
}

static void test_utf8_truncation_stops_before_partial_codepoint(void) {
    char body[2050];
    parsed_message_t msg;

    memset(body, 'a', 2044);
    memcpy(body + 2044, "😀", 4);
    body[2048] = '\0';
    parser_split("sender", body, &msg);

    assert(strlen(msg.content) == 2044);
    assert(msg.content[2043] == 'a');
    assert(msg.content[2044] == '\0');
}

static void test_invalid_utf8_is_replaced(void) {
    const char malformed[] = {'a', (char)0xff, 'b', '\0'};
    parsed_message_t msg;

    parser_split("sender", malformed, &msg);
    assert(strcmp(msg.content, "a?b") == 0);
}

int main(void) {
    test_direct_message();
    test_group_message();
    test_empty_fields();
    test_body_without_sender_prefix_stays_direct();
    test_overlong_sender_prefix_stays_direct();
    test_unicode_is_preserved();
    test_utf8_truncation_stops_before_partial_codepoint();
    test_invalid_utf8_is_replaced();

    printf("parser tests passed\n");
    return 0;
}
