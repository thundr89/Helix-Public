/*
 * CoD4 1.7 connectionless handshake. The bytes match iw3mp 1.7.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef COD4_HANDSHAKE_H
#define COD4_HANDSHAKE_H

#include <stdint.h>

#define COD4_PROTOCOL 6
#define COD4_HANDSHAKE_PEERS 64

typedef struct Cod4Peer {
    uint32_t ip;
    uint16_t port;
    uint16_t qport;
    int32_t challenge;
    int live;
    int connected;
} Cod4Peer;

typedef struct Cod4Handshake {
    Cod4Peer peers[COD4_HANDSHAKE_PEERS];
    int32_t next_challenge;
    int slots_free;
    char game[64];
} Cod4Handshake;

/* game is the fs_game directory. NULL or "" is the base game.
 * Returns 0 when game is longer than 63 bytes or contains a space,
 * quote, backslash, CR, or LF. */
int cod4_handshake_init(Cod4Handshake* sv, int slots_free, const char* game);

/* Writes one reply, without a trailing NUL in the returned length.
 * Returns the reply size, 0 when the packet gets no reply, or -1
 * when an argument is unusable. */
int cod4_handshake_packet(Cod4Handshake* sv, uint32_t ip, uint16_t port,
                          const void* packet, int len,
                          unsigned char* reply, int reply_cap);

/* The qport stored by a successful connect, or -1. */
int cod4_handshake_qport(const Cod4Handshake* sv, uint32_t ip, uint16_t port);

#endif
