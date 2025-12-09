#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h> // For waitpid if using fork()
#include "nimd.h"

#define MAX_PENDING_CONNECTIONS 2 

// Global list to track active players (for Error 23/24 check in a concurrent server).
// Since this version runs a single game and closes, the check is simplified.
// For the single-game version, we simply track if two clients are connected.

/**
 * @brief Sets up the listening TCP socket.
 * @param port The port number to listen on.
 * @return The listening socket file descriptor, or -1 on error.
 */
int setup_listening_socket(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Error creating socket");
        return -1;
    }

    // Allow the socket to be reused immediately after closure // this is added to enhance the program.
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(listen_fd);
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error binding socket");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("Error listening on to the socket");
        close(listen_fd);
        return -1;
    }
    
    printf("Server listening on port %d.\n", port);
    return listen_fd;
}

/**
 * @brief Handles the initial NGP handshake and client queueing.
 * @param client_fd The new connection's socket descriptor.
 * @param client_info Pointer to the Client structure to fill.
 * @return 1 on successful handshake, 0 on failure/close.
 */
int handle_handshake(int client_fd, Client *client_info) {
    NGPMessage msg;
    int status = receive_ngp_message(client_fd, &msg);

    if (status <= 0) {
        // Disconnect or framing error (Error 10)
        if (status == -1) {
            send_fail_and_close(client_fd, "10", "Invalid");
        } else {
            close(client_fd);
        }
        return 0;
    }

    if (msg.type != MSG_OPEN) {
        // Expected OPEN, got error (Error 24: Not Playing)
        send_fail_and_close(client_fd, "24", "Not Playing");
        return 0;
    }

    // OPEN field 1 is the player name
    const char *name = msg.fields[0];
    if (strlen(name) > MAX_NAME_LEN) {
        // Error 21: Long Name
        send_fail_and_close(client_fd, "21", "Long Name");
        return 0;
    }

    // Handshake complete, fill client info
    client_info->fd = client_fd;
    strncpy(client_info->name, name, MAX_NAME_LEN);
    client_info->name[MAX_NAME_LEN] = '\0';
    
    return 1;
}

/**
 * @brief Main function of the Nim Daemon.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    if (port <= 1024) {
        fprintf(stderr, "Error: Port number must be greater than 1024.\n");
        return 1;
    }
    
    int listen_fd = setup_listening_socket(port);
    if (listen_fd < 0) {
        return 1;
    }

    // Storage for the two clients required for a single game (P1 and P2)
    Client player1 = { .fd = -1, .player_num = 1 };
    Client player2 = { .fd = -1, .player_num = 2 };

    while (1) {
        if (player1.fd == -1 || player2.fd == -1) {
            
            // Determine which slot is free
            Client *current_client = (player1.fd == -1) ? &player1 : &player2;
            
            // Accept connection
            struct sockaddr_in cli_addr;
            socklen_t clilen = sizeof(cli_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
            
            if (client_fd < 0) {
                perror("Error accepting connection");
                continue;
            }
            printf("[nimd] Connection accepted from %s. FD: %d\n", inet_ntoa(cli_addr.sin_addr), client_fd);
            
            // Handshake (OPEN message)
            if (!handle_handshake(client_fd, current_client)) {
                // Handshake failed, client socket already closed by handler
                printf("[nimd] Handshake failed, waiting for next client.\n");
                current_client->fd = -1; // Reset slot
                continue;
            }
            
            printf("[nimd] Player %d (%s) connected. Sending WAIT.\n", current_client->player_num, current_client->name);

            // Send WAIT message 
            send_ngp_message(current_client->fd, MSG_WAIT);

            // Check if both slots are now filled
            if (player1.fd != -1 && player2.fd != -1) {
                printf("[nimd] Two players matched! Starting game.\n");
                
                // --- START GAME ---
                // For the single-game version, we run the game in the main process and block until it's done.
                
                // NOTE: If using fork(), you must close the listening socket in the child process.
                // For simplicity, we are running it inline here.
                
                run_single_game(&player1, &player2);
                
                printf("[nimd] Game finished. Resetting server to wait for new players.\n");
                
                // Reset client slots to wait for the next two players,
                player1.fd = -1;
                player2.fd = -1;
            }
        } else {
            // Both slots are full, but the game is still running (wait for it to finish)
            
            // Since this is a single-game server, we intentionally wait here.
            sleep(1); 
        }
    }

    close(listen_fd);
    return 0;
}
