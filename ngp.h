#ifndef NGP_H
#define NGP_H

#include <stddef.h>

#define NGP_MAX_FIELDS 8

typedef struct {
    char type[5];           // "OPEN", "MOVE", etc, null-terminated
    int  field_count;
    char *fields[NGP_MAX_FIELDS]; // pointers into the original buffer
} ngp_message;

// Parse one complete NGP message from buf[0..len-1] into msg.
// Returns 0 on success, non-zero on error.
int ngp_parse(char *buf, size_t len, ngp_message *msg);

// Build simple messages
size_t ngp_build_wait(char *buf, size_t cap);
size_t ngp_build_fail(char *buf, size_t cap, int code, const char *msg);

// New helpers for full protocol
size_t ngp_build_name(char *buf, size_t cap,
                      int player_num, const char *opponent_name);

size_t ngp_build_play(char *buf, size_t cap,
                      int next_player, const char *board_str);

size_t ngp_build_over(char *buf, size_t cap,
                      int winner, const char *board_str, int forfeit);

#endif
