#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nimd.h"

// ---  Board Utilities ---

// Initialize board with the standard configuration: 1, 3, 5, 7, 9 stones
void initialize_board(BoardState *board) {
    board->piles[0] = 1;
    board->piles[1] = 3;
    board->piles[2] = 5;
    board->piles[3] = 7;
    board->piles[4] = 9;
    board->total_stones = 1 + 3 + 5 + 7 + 9; // 25
}

// Convert BoardState struct to NGP string format "P1 P2 P3 P4 P5"
char* board_to_string(const BoardState *board) {
    static char str[50]; 
    snprintf(str, 50, "%d %d %d %d %d", 
             board->piles[0], board->piles[1], board->piles[2], 
             board->piles[3], board->piles[4]);
    return str;
}

// ---  Move Validation and Application ---

/** point to remember - 
 * @brief Validates a MOVE (pile and quantity).
 * @param board This will be the current board state.
 * @param pile_index The 1-based index of the pile (1-5).
 * @param quantity The number of stones to remove.
 * @return 0 on success, 32 for Pile Index error, 33 for Quantity errors.
 */
int is_valid_move(const BoardState *board, int pile_index, int quantity) {
    // 1. Pile index check (1-based index)
    if (pile_index < 1 || pile_index > NUM_PILES) {
        return 32; // 32 Pile Index
    }

    int pile_stones = board->piles[pile_index - 1]; // Convert to 0-based array index

    // 2. Quantity check: Must be > 0 and <= stones in the pile
    if (quantity <= 0 || quantity > pile_stones) {
        return 33; // 33 Quantity
    }

    return 0; 
}

// Applies the validated move to the board state
void apply_move(BoardState *board, int pile_index, int quantity) {
    board->piles[pile_index - 1] -= quantity;
    board->total_stones -= quantity;
}

// ---  Game Runner ---

/**
 * @brief Manages the flow of a single game between two connected clients.
 * * @param p1 Client structure for Player one.
 * @param p2 Client structure for Player two.
 */
void run_single_game(Client *p1, Client *p2) {
    BoardState board;
    initialize_board(&board);
    int current_turn = 1; // Player 1 starts
    
    printf("[P2] Starting game between %s (P1) and %s (P2).\n", p1->name, p2->name);

    // 1. Send NAME to both players
    char p1_num_str[2], p2_num_str[2];
    sprintf(p1_num_str, "%d", 1);
    sprintf(p2_num_str, "%d", 2);
    
    // P1 (1) is playing against P2's name // printing the message
    send_ngp_message(p1->fd, MSG_NAME, p1_num_str, p2->name); 
    // P2 (2) is playing against P1's name // printing the messsage 
    send_ngp_message(p2->fd, MSG_NAME, p2_num_str, p1->name); 

    while (board.total_stones > 0) {
        Client *current_player = (current_turn == 1) ? p1 : p2;
        Client *opponent_player = (current_turn == 1) ? p2 : p1;
        
        printf("[P2] Board state: %s. It's Player %d (%s)'s turn.\n", 
               board_to_string(&board), current_turn, current_player->name);

        // 2. Send PLAY to both players
        char turn_str[2];
        sprintf(turn_str, "%d", current_turn);
        char *board_str = board_to_string(&board);
        
        send_ngp_message(p1->fd, MSG_PLAY, turn_str, board_str);
        send_ngp_message(p2->fd, MSG_PLAY, turn_str, board_str);

        NGPMessage move_msg;
        
        // 3. Wait for MOVE from the current player
        int received = receive_ngp_message(current_player->fd, &move_msg); 

        if (received <= 0) {
            // Disconnect(The other player wins)
            int winner_num = (current_turn == 1) ? 2 : 1;
            char winner_num_str[2];
            sprintf(winner_num_str, "%d", winner_num);
            
            // Send OVER to the winner
            send_ngp_message(opponent_player->fd, MSG_OVER, winner_num_str, board_to_string(&board), "Forfeit");
            printf("[P2] Player %d (%s) forfeited. Player %d (%s) wins.\n", 
                   current_turn, current_player->name, winner_num, opponent_player->name);
            
            // Close the forfeited client's socket
            close(current_player->fd);
            break; 
        }

        if (move_msg.type == MSG_MOVE) {
            // MOVE fields: F1=PileIndex, F2=Quantity
            int pile = atoi(move_msg.fields[0]);
            int quantity = atoi(move_msg.fields[1]);
            
            int error_code = is_valid_move(&board, pile, quantity);
            
            if (error_code == 0) {
                apply_move(&board, pile, quantity);
                current_turn = (current_turn == 1) ? 2 : 1; // Switch the turn between players
            } else {
                // Send FAIL and close connection on move errors (32 or 33)
                if (error_code == 32) {
                    send_fail_and_close(current_player->fd, "32", "Pile Index");
                } else if (error_code == 33) {
                    send_fail_and_close(current_player->fd, "33", "Quantity");
                }
                // The opponent wins by default when the other player is disconnected by a FAIL
                int winner_num = opponent_player->player_num;
                char winner_num_str[2];
                sprintf(winner_num_str, "%d", winner_num);
                send_ngp_message(opponent_player->fd, MSG_OVER, winner_num_str, board_to_string(&board), "Opponent Invalid Move");
                
                // Close the remaining opponent connection after informing them
                close(opponent_player->fd);
                return; // End game
            }
        } else {
            // Received an unexpected message (e.g., NAME, PLAY, etc.) during MOVE phase
            send_fail_and_close(current_player->fd, "24", "Not Playing");
            // Opponent wins by default
            char winner_num_str[2];
            sprintf(winner_num_str, "%d", opponent_player->player_num);
            send_ngp_message(opponent_player->fd, MSG_OVER, winner_num_str, board_to_string(&board), "Opponent Protocol Error");
            close(opponent_player->fd);
            return; // End game
        }
    } // End of game loop

    // 4. Game concluded (board.total_stones == 0)
    if (board.total_stones == 0) {
        // The winner is the player who just made the move (NOT the current_turn player). This is important to avoid the erros of making the wrong player win.
        int winner_num = (current_turn == 1) ? 2 : 1; 
        char winner_num_str[2];
        sprintf(winner_num_str, "%d", winner_num);
        
        Client *winner = (winner_num == 1) ? p1 : p2;
        Client *loser = (winner_num == 1) ? p2 : p1;

        printf("[P2] Game over. Player %d (%s) removed the last stone and wins! yay.\n", winner_num, winner->name);

        // Send OVER to both and close
        send_ngp_message(winner->fd, MSG_OVER, winner_num_str, board_to_string(&board), "");
        send_ngp_message(loser->fd, MSG_OVER, winner_num_str, board_to_string(&board), "");
    }
    
    // Ensure all sockets are closed
    close(p1->fd);
    close(p2->fd);
}
