# Linux Network Dictionary

A Linux-only client/server dictionary application written in C.

## What changed

This version focuses on three improvements:

1. Reliable `MSG` protocol send/receive with short-read and short-write handling.
2. Server-side authenticated session state, salted SHA-256 password storage, and safer input handling.
3. Per-child SQLite connections, in-memory dictionary indexing, a `Makefile`, smoke tests, and repository cleanup.

## Requirements

- Linux
- `gcc`
- `libsqlite3-dev`
- `libssl-dev`
- `python3` for the smoke test

Example installation on Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install build-essential libsqlite3-dev libssl-dev python3
```

## Project layout

```text
.
├── dict_client.c
├── dict_common.c
├── dict_common.h
├── dict_server.c
├── dict.txt
├── Makefile
├── README.md
└── tests
    ├── smoke_client.py
    └── smoke_test.sh
```

## Build

```bash
make
```

Build targets:

- `make all`
- `make server`
- `make client`
- `make clean`

## Run

Start the server:

```bash
./dict_server 127.0.0.1 8888
```

Start the client in another terminal:

```bash
./dict_client 127.0.0.1 8888
```

## Protocol

The client and server still exchange the same fixed-width `MSG` structure:

```c
typedef struct {
    int type;
    char name[16];
    char data[256];
} MSG;
```

The implementation now uses full-length send/receive helpers so one logical message is always transferred as one complete `MSG` payload.

Message types:

- `1`: register
- `2`: login
- `3`: query word
- `4`: query history

Status payloads:

- `OK`
- `NOT_FOUND`
- `NO_HISTORY`
- `ERR_NOT_AUTHENTICATED`
- `ERR_INVALID_INPUT`
- `ERR_INTERNAL`
- `ERR_USER_EXISTS`
- `ERR_INVALID_CREDENTIALS`

History responses end with `**OVER**`.

## Authentication and storage

- Usernames must be 1-15 characters and use only letters, numbers, or underscore.
- Passwords are not stored in plaintext.
- The server generates a random salt and stores:
  - `salt_hex`
  - `pass_hash_hex`
- Hash algorithm: `SHA-256(salt + password)`

SQLite tables:

- `usr(name, salt_hex, pass_hash_hex)`
- `record(id, name, created_at, word)`

Notes:

- The database schema is rebuilt automatically if an old schema version is found.
- This project intentionally allows `my.db` to be recreated instead of migrating old plaintext credentials.

## Dictionary behavior

- `dict.txt` is loaded once at server startup.
- Words are indexed in an in-memory hash table.
- Query requests no longer reopen and rescan the file for every search.
- Invalid or malformed dictionary lines are skipped during load.

Dictionary file format:

```text
word meaning
```

The repository dictionary content should remain UTF-8 encoded.

## Logging

The server prints lightweight operational logs to stderr/stdout, including:

- startup
- dictionary load count
- client connect/disconnect
- authentication success/failure
- query hit/miss
- database errors

## Smoke test

The smoke test builds the project, starts the server, runs scripted protocol checks, and validates the SQLite state.

```bash
bash tests/smoke_test.sh
```

Covered scenarios:

- unauthenticated query rejection
- register
- login
- query hit
- query miss
- history retrieval
- history retention capped at 10 rows
- password storage is hashed and salted

## Important behavior

- The server keeps authenticated identity in the server-side session for each connection.
- Query and history requests ignore any forged username provided by the client.
- The project is Linux-only. Password input depends on `termios`.
