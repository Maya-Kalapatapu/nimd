#include "game.h"

void game_init(game_t *g) {
    int defaults[NIM_PILES] = {1, 3, 5, 7, 9};
    for (int i = 0; i < NIM_PILES; i++) {
        g->piles[i] = defaults[i];
    }
    g->current_player = 1;
}

int game_is_over(const game_t *g) {
    for (int i = 0; i < NIM_PILES; i++) {
        if (g->piles[i] > 0)
            return 0;
    }
    return 1;
}

int game_is_valid_move(const game_t *g, int pile, int qty) {
    if (pile < 0 || pile >= NIM_PILES)
        return 0;
    if (qty <= 0)
        return 0;
    if (qty > g->piles[pile])
        return 0;
    return 1;
}

void game_apply_move(game_t *g, int pile, int qty) {
    g->piles[pile] -= qty;
    g->current_player = (g->current_player == 1 ? 2 : 1);
}
