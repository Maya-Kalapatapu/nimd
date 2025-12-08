#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <pthread.h>

#include "network.h"
#include "ngp.h"
#include "game.h"

#define BUF_SIZE 512
#define MAX_NAME_LEN 72   // per spec
#define MAX_WAITING 16    // max lobby size

/* Check whether a socket is still alive (no disconnect yet). */
static int fd_alive(int fd) {
    char c;
    ssize_t n = recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) {
        /* peer has closed the connection */
        return 0;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* no data, but connection is still open */
            return 1;
        }
        /* other error: treat as dead */
        return 0;
    }
    /* n > 0: there is data, so it's alive */
    return 1;
}


typedef struct {
    int  fd;
    char name[MAX_NAME_LEN + 1];
} player_t;

/* global list of players currently in a game, for FAIL 22 Already Playing */

typedef struct active_player {
    char name[MAX_NAME_LEN + 1];
    struct active_player *next;
} active_player_t;

static active_player_t *active_head = NULL;
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;

/* waiting lobby (not yet in a game) */
static player_t waiting[MAX_WAITING];
static int waiting_count = 0;

/* utility: format board as "a b c d e" */
static void format_board(const game_t *g, char *buf, size_t cap) {
    snprintf(buf, cap, "%d %d %d %d %d",
             g->piles[0], g->piles[1], g->piles[2],
             g->piles[3], g->piles[4]);
}

/* utility: read and parse a single NGP message from fd */
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

/* active player list helpers (must be called with active_mutex held) */

static int active_name_in_use_locked(const char *name) {
    for (active_player_t *p = active_head; p; p = p->next) {
        if (strcmp(p->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void active_add_locked(const char *name) {
    active_player_t *node = malloc(sizeof(*node));
    if (!node) return;
    strncpy(node->name, name, MAX_NAME_LEN);
    node->name[MAX_NAME_LEN] = '\0';
    node->next = active_head;
    active_head = node;
}

static void active_remove_locked(const char *name) {
    active_player_t **pp = &active_head;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            active_player_t *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* struct passed to each game thread */

typedef struct {
    player_t p1;
    player_t p2;
} game_pair_t;

/* full Nim game between p1 and p2 (runs in its own thread) */
static void run_game(player_t *p1, player_t *p2) {
    game_t game;
    game_init(&game);

    printf("Starting game between '%s' and '%s'\n", p1->name, p2->name);

    char out[256];
    char board[64];
    ngp_message msg;
    char inbuf[BUF_SIZE];
    size_t outlen;

    /* send NAME to each player */
    outlen = ngp_build_name(out, sizeof(out), 1, p2->name);
    (void)write(p1->fd, out, outlen);

    outlen = ngp_build_name(out, sizeof(out), 2, p1->name);
    (void)write(p2->fd, out, outlen);

    /* main turn loop */
    while (!game_is_over(&game)) {
        /* 1. send PLAY to both with current player + board */
        format_board(&game, board, sizeof(board));
        outlen = ngp_build_play(out, sizeof(out), game.current_player, board);
        (void)write(p1->fd, out, outlen);
        (void)write(p2->fd, out, outlen);

        player_t *current = (game.current_player == 1) ? p1 : p2;
        player_t *other   = (game.current_player == 1) ? p2 : p1;

        /* 2. wait for a valid MOVE from the current player, but
           also watch the other player for out-of-turn or disconnect. */
        for (;;) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(current->fd, &rfds);
            FD_SET(other->fd, &rfds);
            int maxfd = (current->fd > other->fd) ? current->fd : other->fd;

            int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
            if (rc < 0) {
                if (errno == EINTR) continue;
                /* fatal select error: end game */
                close(p1->fd);
                close(p2->fd);
                return;
            }

            /* handle other player's activity first: Impatient / disconnect */
            if (FD_ISSET(other->fd, &rfds)) {
                if (read_ngp(other->fd, inbuf, sizeof(inbuf), &msg) != 0) {
                    /* other disconnected; current wins by forfeit */
                    printf("%s disconnected; %s wins by forfeit\n",
                           other->name, current->name);
                    format_board(&game, board, sizeof(board));
                    outlen = ngp_build_over(out, sizeof(out),
                                            (current == p1) ? 1 : 2,
                                            board, 1);
                    (void)write(current->fd, out, outlen);
                    close(p1->fd);
                    close(p2->fd);
                    return;
                }

                if (strcmp(msg.type, "MOVE") == 0) {
                    /* out-of-turn MOVE => FAIL 31 Impatient */
                    outlen = ngp_build_fail(out, sizeof(out),
                                            31, "Impatient");
                    (void)write(other->fd, out, outlen);
                    /* do not change turn; loop again */
                    continue;
                } else if (strcmp(msg.type, "OPEN") == 0) {
                    /* Already Open during game */
                    outlen = ngp_build_fail(out, sizeof(out),
                                            23, "Already Open");
                    (void)write(other->fd, out, outlen);

                    /* current wins by forfeit */
                    format_board(&game, board, sizeof(board));
                    outlen = ngp_build_over(out, sizeof(out),
                                            (current == p1) ? 1 : 2,
                                            board, 1);
                    (void)write(current->fd, out, outlen);
                    close(p1->fd);
                    close(p2->fd);
                    return;
                } else {
                    /* any other message from other => general invalid + forfeit */
                    outlen = ngp_build_fail(out, sizeof(out),
                                            10, "Invalid");
                    (void)write(other->fd, out, outlen);
                    format_board(&game, board, sizeof(board));
                    outlen = ngp_build_over(out, sizeof(out),
                                            (current == p1) ? 1 : 2,
                                            board, 1);
                    (void)write(current->fd, out, outlen);
                    close(p1->fd);
                    close(p2->fd);
                    return;
                }
            }

            /* now handle current player's move, if ready */
            if (FD_ISSET(current->fd, &rfds)) {
                if (read_ngp(current->fd, inbuf, sizeof(inbuf), &msg) != 0) {
                    /* current disconnected; other wins by forfeit */
                    printf("%s disconnected; %s wins by forfeit\n",
                           current->name, other->name);
                    format_board(&game, board, sizeof(board));
                    outlen = ngp_build_over(out, sizeof(out),
                                            (current == p1) ? 2 : 1,
                                            board, 1);
                    (void)write(other->fd, out, outlen);
                    close(p1->fd);
                    close(p2->fd);
                    return;
                }

                if (strcmp(msg.type, "MOVE") == 0 && msg.field_count >= 2) {
                    /* fall through to parse/validate below */
                } else if (strcmp(msg.type, "OPEN") == 0) {
                    outlen = ngp_build_fail(out, sizeof(out),
                                            23, "Already Open");
                    (void)write(current->fd, out, outlen);
                    format_board(&game, board, sizeof(board));
                    outlen = ngp_build_over(out, sizeof(out),
                                            (current == p1) ? 2 : 1,
                                            board, 1);
                    (void)write(other->fd, out, outlen);
                    close(p1->fd);
                    close(p2->fd);
                    return;
                } else {
                    /* wrong type in-game from current => invalid + forfeit */
                    outlen = ngp_build_fail(out, sizeof(out),
                                            10, "Invalid");
                    (void)write(current->fd, out, outlen);
                    format_board(&game, board, sizeof(board));
                    outlen = ngp_build_over(out, sizeof(out),
                                            (current == p1) ? 2 : 1,
                                            board, 1);
                    (void)write(other->fd, out, outlen);
                    close(p1->fd);
                    close(p2->fd);
                    return;
                }

                /* parse pile and quantity */
                char *endptr;
                int pile = (int)strtol(msg.fields[0], &endptr, 10);
                if (*endptr != '\0') pile = -1;

                int qty = (int)strtol(msg.fields[1], &endptr, 10);
                if (*endptr != '\0') qty = -1;

                /* validate move: index vs quantity to choose error codes */
                if (pile < 0 || pile >= NIM_PILES) {
                    outlen = ngp_build_fail(out, sizeof(out),
                                            32, "Pile Index");
                    (void)write(current->fd, out, outlen);
                    /* do NOT change turn; ask again */
                    continue;
                }

                if (qty <= 0 || qty > game.piles[pile]) {
                    outlen = ngp_build_fail(out, sizeof(out),
                                            33, "Quantity");
                    (void)write(current->fd, out, outlen);
                    continue;
                }

                /* apply move */
                game_apply_move(&game, pile, qty);

                /* finished a valid move, break inner loop to check game over */
                break;
            }

            /* if we got here with no fd ready (shouldn't happen), loop again */
        }

        /* after a valid move, check for end of game */
        if (game_is_over(&game)) {
            int winner = (game.current_player == 1) ? 2 : 1;
            format_board(&game, board, sizeof(board));
            outlen = ngp_build_over(out, sizeof(out),
                                    winner, board, 0);
            (void)write(p1->fd, out, outlen);
            (void)write(p2->fd, out, outlen);
            close(p1->fd);
            close(p2->fd);
            return;
        }

        /* otherwise, next iteration: game.current_player already flipped
           by game_apply_move. */
    }

    close(p1->fd);
    close(p2->fd);
}

/* thread entry: run a game, then remove players from active list */
static void *game_thread(void *arg) {
    game_pair_t pair = *(game_pair_t *)arg;
    free(arg);

    run_game(&pair.p1, &pair.p2);

    pthread_mutex_lock(&active_mutex);
    active_remove_locked(pair.p1.name);
    active_remove_locked(pair.p2.name);
    pthread_mutex_unlock(&active_mutex);

    return NULL;
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
            size_t outlen = ngp_build_fail(out, sizeof(out),
                                           10, "Invalid");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        if (strcmp(msg.type, "OPEN") == 0) {
            /* continue below */
        } else if (strcmp(msg.type, "MOVE") == 0) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out),
                                           24, "Not Playing");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        } else {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out),
                                           10, "Invalid");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        if (msg.field_count < 1) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out),
                                           10, "Invalid");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        const char *name = msg.fields[0];
        size_t name_len = strlen(name);
        if (name_len == 0 || name_len > MAX_NAME_LEN) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out),
                                           21, "Long Name");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        /* check 22 Already Playing: name already in an active game,
           or already in the waiting lobby. */
        int name_in_use = 0;
        pthread_mutex_lock(&active_mutex);
        if (active_name_in_use_locked(name)) {
            name_in_use = 1;
        } else {
            for (int i = 0; i < waiting_count; i++) {
                if (strcmp(waiting[i].name, name) == 0) {
                    name_in_use = 1;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&active_mutex);

        if (name_in_use) {
            char out[128];
            size_t outlen = ngp_build_fail(out, sizeof(out),
                                           22, "Already Playing");
            (void)write(fd, out, outlen);
            close(fd);
            continue;
        }

        /* send WAIT to this client */
        char out[128];
        size_t outlen = ngp_build_wait(out, sizeof(out));
        (void)write(fd, out, outlen);

        /* add to waiting lobby */
        if (waiting_count < MAX_WAITING) {
            waiting[waiting_count].fd = fd;
            strncpy(waiting[waiting_count].name, name, MAX_NAME_LEN);
            waiting[waiting_count].name[MAX_NAME_LEN] = '\0';
            waiting_count++;
        } else {
            /* lobby full; just close connection */
            close(fd);
        }

        /* prune any waiting players whose connections died before game */
        int write_idx = 0;
        for (int i = 0; i < waiting_count; i++) {
            if (fd_alive(waiting[i].fd)) {
                if (write_idx != i) {
                    waiting[write_idx] = waiting[i];
                }
                write_idx++;
            } else {
                /* client disconnected before game; drop them */
                close(waiting[i].fd);
            }
        }
        waiting_count = write_idx;

        /* if we have at least two waiting players, start games in new threads */
        while (waiting_count >= 2) {
            game_pair_t *pair = malloc(sizeof(*pair));
            if (!pair) {
                /* out of memory; try again later */
                break;
            }

            pair->p1 = waiting[0];
            pair->p2 = waiting[1];

            /* shift remaining waiting players down */
            for (int i = 2; i < waiting_count; i++) {
                waiting[i - 2] = waiting[i];
            }
            waiting_count -= 2;

            pthread_mutex_lock(&active_mutex);
            active_add_locked(pair->p1.name);
            active_add_locked(pair->p2.name);
            pthread_mutex_unlock(&active_mutex);

            pthread_t tid;
            if (pthread_create(&tid, NULL, game_thread, pair) != 0) {
                perror("pthread_create");
                close(pair->p1.fd);
                close(pair->p2.fd);
                free(pair);
                continue;
            }

            pthread_detach(tid);
        }
    }
}
