#include "ngp.h"

#include <string.h>
#include <stdio.h>

// --------------------------
// Parse NGP message
// --------------------------

int ngp_parse(char *buf, size_t len, ngp_message *msg) {
    if (len == 0 || buf[len - 1] != '|') {
        return -1;
    }

    char *p   = buf;
    char *end = buf + len;

    // version
    char *vsep = memchr(p, '|', end - p);
    if (!vsep) return -1;
    p = vsep + 1;

    // length field
    char *lsep = memchr(p, '|', end - p);
    if (!lsep) return -1;
    p = lsep + 1;

    // TYPE (4 chars)
    if (end - p < 4) return -1;
    memcpy(msg->type, p, 4);
    msg->type[4] = '\0';
    p += 4;

    if (p >= end || *p != '|') return -1;
    p++; // past TYPE separator

    msg->field_count = 0;
    while (p < end - 1 && msg->field_count < NGP_MAX_FIELDS) {
        char *next = memchr(p, '|', end - p);
        if (!next) return -1;

        *next = '\0'; // terminate field
        msg->fields[msg->field_count++] = p;
        p = next + 1;
    }

    return 0;
}

// --------------------------
// Build WAIT
// --------------------------

size_t ngp_build_wait(char *buf, size_t cap) {
    const char *body = "WAIT|";
    int body_len = (int)strlen(body); // bytes after length field

    int written = snprintf(buf, cap, "0|%02d|%s", body_len, body);
    if (written < 0) return 0;
    return (size_t)written;
}

// --------------------------
// Build FAIL
// --------------------------

size_t ngp_build_fail(char *buf, size_t cap, int code, const char *msg) {
    char body[256];
    int blen = snprintf(body, sizeof(body), "FAIL|%d %s|", code, msg);
    if (blen < 0) return 0;

    int written = snprintf(buf, cap, "0|%02d|%s", blen, body);
    if (written < 0) return 0;
    return (size_t)written;
}

// --------------------------
// Build NAME
// NAME|<player_num>|<opponent_name>|
// --------------------------

size_t ngp_build_name(char *buf, size_t cap,
                      int player_num, const char *opponent_name) {
    char body[256];
    int blen = snprintf(body, sizeof(body),
                        "NAME|%d|%s|", player_num, opponent_name);
    if (blen < 0) return 0;

    int written = snprintf(buf, cap, "0|%02d|%s", blen, body);
    if (written < 0) return 0;
    return (size_t)written;
}

// --------------------------
// Build PLAY
// PLAY|<next_player>|<board_str>|
// board_str like "1 3 5 7 9"
// --------------------------

size_t ngp_build_play(char *buf, size_t cap,
                      int next_player, const char *board_str) {
    char body[256];
    int blen = snprintf(body, sizeof(body),
                        "PLAY|%d|%s|", next_player, board_str);
    if (blen < 0) return 0;

    int written = snprintf(buf, cap, "0|%02d|%s", blen, body);
    if (written < 0) return 0;
    return (size_t)written;
}

// --------------------------
// Build OVER
// OVER|<winner>|<board_str>|"" or "Forfeit"|
// Forfeit flag: 0 = normal, non-zero = forfeit
// --------------------------

size_t ngp_build_over(char *buf, size_t cap,
                      int winner, const char *board_str, int forfeit) {
    char body[256];
    const char *reason = forfeit ? "Forfeit" : "";
    int blen = snprintf(body, sizeof(body),
                        "OVER|%d|%s|%s|", winner, board_str, reason);
    if (blen < 0) return 0;

    int written = snprintf(buf, cap, "0|%02d|%s", blen, body);
    if (written < 0) return 0;
    return (size_t)written;
}
