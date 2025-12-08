# nimd — Networked Nim Daemon

This project implements a complete server for the game Nim using the Network Game Protocol (NGP), including multi-game capabilities.

## Core Features (Required)
• Correct handling of NGP message types: OPEN, WAIT, NAME, PLAY, MOVE, OVER, FAIL  
• Valid Nim gameplay with starting piles: 1 3 5 7 9  
• Validates moves and enforces legal rules  
• Proper turn alternation and winner detection  
• Client disconnects cause a Forfeit win for the opponent  
• Correct error implementation:  
  – 10 Invalid  
  – 21 Long Name  
  – 23 Already Open  
  – 24 Not Playing  
  – 32 Pile Index  
  – 33 Quantity  
• Max name length enforced at 72 characters (per spec)  
• Any malformed or incorrectly framed message results in FAIL 10 and the connection closing, as required

To build: run “make”.  
To start the server: run “./nimd <port>”.  
Use “testc” for interactive play or “rawc” for sending raw protocol messages.  
To test: run "make test".  
See Automated testing section below for more details.

## Extra Credit Implemented
### Multi-Game Concurrency
The server supports multiple simultaneous Nim games. A thread-per-game model allows each matched pair of players to run independently while the main thread continues accepting new players.  
Disconnected clients in the waiting lobby are automatically removed before pairing, preventing stale or dead entries.  
This matches the spec’s expected behavior for multi-game servers.

### FAIL 22 — Already Playing
A global thread-safe list tracks all active players and players waiting in the lobby.  
If an OPEN arrives using a name already in use (either waiting or currently in a game), the server returns FAIL 22 Already Playing.

### FAIL 31 — Impatient
Each game uses select() to monitor both player sockets.  
If a player sends MOVE when it is not their turn, the server immediately returns FAIL 31 Impatient without advancing the game.

## Automated Testing (make test)
Running “make test” performs all required and extra-credit protocol tests:  
• MOVE before OPEN → FAIL 24  
• Invalid framing → FAIL 10  
• Overlong names → FAIL 21  
• Reusing a name that is playing or waiting → FAIL 22  
• Out-of-turn MOVE → FAIL 31  
• Bad pile index → FAIL 32  
• Bad quantity → FAIL 33  
• Opponent disconnect mid-game → OVER … Forfeit  

The test script launches fresh server instances for clean, deterministic results.  
All responses are displayed as hexdumps for transparent grading.  
Additional manual tests can also be performed using testc to confirm full game flow, turn alternation, and correct end-of-game behavior.

## File Overview
• nimd.c — server logic, matchmaking, concurrency, protocol handling  
• game.c/h — Nim rules and state transitions  
• ngp.c/h — NGP parsing and message building  
• network.c/h — socket utilities  
• rawc.c — manual protocol client  
• testc — interactive client used to play Nim  
• test_nimd.sh — automated test suite (run with "make test")  
• Makefile — build rules and test target
