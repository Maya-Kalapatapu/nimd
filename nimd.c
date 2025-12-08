#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "network.h"
#include "ngp.h"
#include "game.h"

#define BUF_SIZE 512
#define MAX_NAME_LEN 72   // per spec

typedef struct {
    int  fd;
    char name[MAX_NAME_LEN + 1];
} player_t;

// Format board as "a b c d e"
static void format_board(const game_t *g, char *buf, size_t cap) {
    snprintf(buf, cap, "%d %d %d %d %d",
             g->piles[0], g->piles[1], g->piles[2],
             g->piles[3], g->piles[4]);
}

// Read one NGP message from fd into buf, parse into msg.
// Returns 0 on success, -1 on EOF/error.
static int read_ngp(int fd, char *buf, size_t cap, ngp_message *msg) {
    ssize_t n = read(fd, buf, cap);
    if (n <= 0) {
        return -1;
    }
    if (ngp_parse(buf, (size_t)n, msg) != 0) {
        return -1;
    }
    return 0;
}

/* Full Nim game between p1 and p2 (single game, blocking). */
static void run_game(player_t *p1, player_t *p2) {
    game_t game;
    game_init(&game);

    printf("Starting game between '%s' and '%s'\n", p1->name, p2->name);

    char out[256];
    char board[64];
    ngp_message msg;
    char inbuf[BUF_SIZE];

    // Send NAME to each player
    size_t outlen;

    outlen = ngp_build_name(out, sizeof(out), 1, p2->name);
    (void)write(p1->fd, out, outlen);

    outlen = ngp_build_name(out, sizeof(out), 2, p1->name);
    (void)write(p2->fd, out, outlen);

    // Main turn loop
    while (!game_is_over(&game)) {
        // 1. Send PLAY to both, with next player and board state
        format_board(&game, board, sizeof(board));

        outlen = ngp_build_play(out, sizeof(out), game.current_player, board);
        (void)write(p1->fd, out, outlen);
        (void)write(p2->fd, out, outlen);

        // 2. Determine whose turn it is
        player_t *current = (game.current_player == 1) ? p1 : p2;
        player_t *other   = (game.current_player == 1) ? p2 : p1;

        // 3. Wait for MOVE from current player
        if (read_ngp(current->fd, inbuf, sizeof(inbuf), &msg) != 0) {
            // Disconnect -> other player wins by forfeit
            printf("%s disconnected; %s wins by forfeit\n",
                   current->name, other->name);

            format_board(&game, board, sizeof(board));
            outlen = ngp_build_over(out, sizeof(out),
                                    (current == p1) ? 2 : 1,
                                    board, 1); // forfeit
            (void)write(other->fd, out, outlen);

            close(p1->fd);
            close(p2->fd);
            return;
        }

        if (strcmp(msg.type, "MOVE") == 0 && msg.field_count >= 2) {
            // ok, handle MOVE below
        } else if (strcmp(msg.type, "OPEN") == 0) {
            // Already Open: this client is already in a game
            outlen = ngp_build_fail(out, sizeof(out), 23, "Already Open");
            (void)write(current->fd, out, outlen);

            // Other player wins by forfeit
            format_board(&game, board, sizeof(board));
            outlen = ngp_build_over(out, sizeof(out),
                                    (current == p1) ? 2 : 1,
                                    board, 1);
            (void)write(other->fd, out, outlen);

            close(p1->fd);
            close(p2->fd);
            return;
        } else {
            // Wrong type / malformed MOVE in-game: treat as general Invalid
            outlen = ngp_build_fail(out, sizeof(out), 10, "Invalid");
            (void)write(current->fd, out, outlen);

            // Other wins by forfeit
            format_board(&game, board, sizeof(board));
            outlen = ngp_build_over(out, sizeof(out),
                                    (current == p1) ? 2 : 1,
                                    board, 1);
            (void)write(other->fd, out, outlen);

            close(p1->fd);
            close(p2->fd);
            return;
        }


        // Parse pile and quantity
        char *endptr;
        int pile = (int)strtol(msg.fields[0], &endptr, 10);
        if (*endptr != '\0') pile = -1;

        int qty = (int)strtol(msg.fields[1], &endptr, 10);
        if (*endptr != '\0') qty = -1;

        // Validate move: index vs quantity to choose error codes
        if (pile < 0 || pile >= NIM_PILES) {
            outlen = ngp_build_fail(out, sizeof(out), 32, "Pile Index");
            (void)write(current->fd, out, outlen);
            // Do NOT switch turns; ask again in next iteration.
            continue;
        }

        if (qty <= 0 || qty > game.piles[pile]) {
            outlen = ngp_build_fail(out, sizeof(out), 33, "Quantity");
            (void)write(current->fd, out, outlen);
            continue;
        }

        // Apply move
        game_apply_move(&game, pile, qty);

        // Check for end of game
        if (game_is_over(&game)) {
            int winner = (current == p1) ? 1 : 2;
            format_board(&game, board, sizeof(board));
            outlen = ngp_build_over(out, sizeof(out),
                                    winner, board, 0); // normal win
            (void)write(p1->fd, out, outlen);
            (void)write(p2->fd, out, outlen);

            close(p1->fd);
            close(p2->fd);
            return;
        }

        // Otherwise, loop continues; game.current_player already flipped
    }

    // Shouldn't really get here (loop exits on game_is_over)
    close(p1->fd);
    close(p2->fd);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int listener = open_listener(argv[1], 10);
    if (listener < 0) {
        fprintf(stderr, "Failed to open listener\n");
        return EXIT_FAILURE;
    }

    printf("nimd listening on %s...\n", argv[1]);

    player_t waiting[2];
    int waiting_count = 0;

    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            perror("accept");
            continue;
        }

        char buf[BUF_SIZE];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            close(fd);
            continue;
        }

        ngp_message msg;
        if (ngp_parse(buf, (size_t)n, &msg) != 0) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out), 10, "Invalid");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        if (strcmp(msg.type, "OPEN") == 0) {
            // fall through to OPEN handling below
        } else if (strcmp(msg.type, "MOVE") == 0) {
            // Client tried to MOVE before being in a game
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out), 24, "Not Playing");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        } else {
            // Any other type as first message is just invalid
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out), 10, "Invalid");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }


        if (msg.field_count < 1) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out), 10, "Invalid");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        const char *name = msg.fields[0];
        size_t name_len = strlen(name);
        if (name_len == 0 || name_len > MAX_NAME_LEN) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out), 21, "Long Name");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        // Send WAIT to this client.
        char out[128];
        size_t outlen = ngp_build_wait(out, sizeof(out));
        (void)write(fd, out, outlen);

        // Store this player in the waiting list, keep the connection open.
        if (waiting_count < 2) {
            waiting[waiting_count].fd = fd;
            strncpy(waiting[waiting_count].name, name, MAX_NAME_LEN);
            waiting[waiting_count].name[MAX_NAME_LEN] = '\0';
            waiting_count++;
        } else {
            // Too many waiting; just close.
            close(fd);
        }

        // If we have two waiting players, start a game and reset the queue.
        if (waiting_count == 2) {
            run_game(&waiting[0], &waiting[1]);
            waiting_count = 0;
        }
    }
}
