#ifndef GAME_H
#define GAME_H

#define NIM_PILES 5

typedef struct {
    int piles[NIM_PILES];   // e.g., {1,3,5,7,9}
    int current_player;     // 1 or 2
} game_t;

// Initialize the Nim board and starting player
void game_init(game_t *g);

// Check if the game is over (all piles empty)
int game_is_over(const game_t *g);

// Validate a move; returns 1 if valid, 0 if invalid
int game_is_valid_move(const game_t *g, int pile, int qty);

// Apply a move (assumes it's valid) and flip current_player
void game_apply_move(game_t *g, int pile, int qty);

#endif
