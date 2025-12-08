# nimd — Networked Nim Daemon (Single-Game Version)

This project implements a server for playing Nim over the Network Game Protocol (NGP). In the single-game mode, the server accepts connections, processes OPEN messages, pairs two clients into a match, runs one complete Nim game, and then returns to waiting for the next pair.

## Features
- Implements NGP messages: OPEN, WAIT, NAME, PLAY, MOVE, OVER, FAIL  
- Nim rules with initial board: 1 3 5 7 9  
- Validates moves and returns appropriate error codes  
- Detects client disconnects and awards Forfeit wins  
- Error codes implemented: 10 Invalid, 21 Long Name, 23 Already Open, 24 Not Playing, 32 Pile Index, 33 Quantity

## Build & Run
Run `make`, then start the server with: `./nimd <port>`

## Clients
Use `testc` for interactive play or `rawc` for manual NGP messages.

## Automated Tests
`make test` runs protocol tests for:
- MOVE before OPEN → FAIL 24  
- Invalid framing → FAIL 10  
- Long names → FAIL 21

## File Overview
nimd.c (server), game.c/h (Nim logic), ngp.c/h (protocol), network.c/h (sockets), rawc.c (manual client), testc (interactive client), test_nimd.sh (automated tests).

## Limitations
Single-game only: server handles one active game at a time and ignores additional clients until the current match ends.

