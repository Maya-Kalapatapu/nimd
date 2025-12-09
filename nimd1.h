#ifndef NIMD_H
#define NIMD_H

#include <stdarg.h> // For variable number of arguments functions
#include <sys/socket.h> // For socket functions

// --- Constants ---
#define MAX_NAME_LEN 72
#define NUM_PILES 5
#define MAX_MSG_BYTES 104 // Max total message length
#define MAX_CONTENT_LEN 99 // MAX_MSG_BYTES - 5 

// --- Enumerations ---
typedef enum {
    MSG_OPEN, MSG_WAIT, MSG_NAME, MSG_PLAY, MSG_MOVE, MSG_OVER, MSG_FAIL
} NGPMessageType;

// --- Data Structures ---

// Structure for the Nim Board State
typedef struct {
    int piles[NUM_PILES]; // Initial: {1, 3, 5, 7, 9}
    int total_stones;
} BoardState;

// Structure for a Client in the setup phase
typedef struct {
    int fd;
    char name[MAX_NAME_LEN + 1];
    int player_num; // 1 or 2
} Client;

// Structure for a parsed NGP message
typedef struct {
    int version;
    int length;
    NGPMessageType type;
    char fields[3][MAX_NAME_LEN + 1]; // Store up to 3 fields (OVER has 3)
    int num_fields;
} NGPMessage;

// --- Protocol Function Prototypes (P1 Focus) ---
const char* get_type_string(NGPMessageType type);
NGPMessageType get_type_enum(const char *type_str);
int get_expected_fields(NGPMessageType type);
int receive_ngp_message(int sockfd, NGPMessage *msg);
void send_ngp_message(int sockfd, NGPMessageType type, ...);
void send_fail_and_close(int sockfd, const char *err_code, const char *err_msg);

// --- Game Function Prototypes (P2 Focus) ---
void initialize_board(BoardState *board);
char* board_to_string(const BoardState *board);
int is_valid_move(const BoardState *board, int pile_index, int quantity);
void apply_move(BoardState *board, int pile_index, int quantity);
void run_single_game(Client *p1, Client *p2);

// --- Networking Function Prototypes ---
int setup_listening_socket(int port);

#endif
