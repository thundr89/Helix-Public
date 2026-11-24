/*
 * Synthetic CoD4 1.7 handshake packets. No retail capture is stored.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "cod4_handshake.h"

#include <stdio.h>
#include <string.h>

static int g_fails;

#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
            g_fails++;                                                           \
        }                                                                        \
    } while (0)

static int packet(unsigned char* dst, const char* text) {
    int n = (int)strlen(text);
    dst[0] = dst[1] = dst[2] = dst[3] = 0xff;
    memcpy(dst + 4, text, (size_t)n);
    return 4 + n;
}

static int same(const unsigned char* reply, int n, const char* text) {
    unsigned char expect[128];
    int want = packet(expect, text);
    return n == want && memcmp(reply, expect, (size_t)n) == 0;
}

static void test_challenge_and_connect(void) {
    Cod4Handshake sv;
    unsigned char in[256];
    unsigned char out[128];
    int n;
    const char* connect =
        "connect \"\\name\\player\\protocol\\6\\qport\\27960\\challenge\\1\"";
    CHECK(cod4_handshake_init(&sv, 1, ""));
    n = cod4_handshake_packet(&sv, 0x7f000001, 28960, in, packet(in, "getchallenge 0 \"token\""), out, 128);
    CHECK(same(out, n, "challengeResponse 1"));
    n = cod4_handshake_packet(&sv, 0x7f000001, 28960, in, packet(in, connect), out, 128);
    CHECK(same(out, n, "connectResponse "));
    CHECK(cod4_handshake_qport(&sv, 0x7f000001, 28960) == 27960);
    CHECK(sv.slots_free == 0);
    n = cod4_handshake_packet(&sv, 0x7f000001, 28960, in, packet(in, connect), out, 128);
    CHECK(same(out, n, "error\nEXE_BAD_CHALLENGE"));
}

static void test_version_and_full(void) {
    Cod4Handshake sv;
    unsigned char in[256];
    unsigned char out[128];
    int n;
    char version[64];
    int k = 0;
    const char* head = "error\nEXE_SERVER_IS_DIFFERENT_VER";
    const char* tail = "1.7\n";
    while (*head) version[k++] = *head++;
    version[k++] = 0x15;
    while (*tail) version[k++] = *tail++;
    version[k] = 0;
    CHECK(cod4_handshake_init(&sv, 0, "mods/demo"));
    n = cod4_handshake_packet(&sv, 1, 2, in, packet(in, "getchallenge"), out, 128);
    CHECK(same(out, n, "challengeResponse 1"));
    n = cod4_handshake_packet(&sv, 1, 2, in,
                              packet(in, "connect \"\\protocol\\1\\challenge\\1\\qport\\1\""), out, 128);
    CHECK(same(out, n, version));
    n = cod4_handshake_packet(&sv, 1, 2, in,
                              packet(in, "connect \"\\protocol\\6\\challenge\\1\\qport\\1\""), out, 128);
    CHECK(same(out, n, "error\nEXE_SERVERISFULL"));
    CHECK(sv.slots_free == 0);
    sv.slots_free = 1;
    n = cod4_handshake_packet(&sv, 1, 2, in,
                              packet(in, "connect \"\\protocol\\6\\challenge\\1\\qport\\40000\""), out, 128);
    CHECK(same(out, n, "connectResponse mods/demo"));
    CHECK(cod4_handshake_qport(&sv, 1, 2) == 40000);
}

static void test_rejects(void) {
    Cod4Handshake sv;
    unsigned char in[64];
    unsigned char out[128];
    int n;
    CHECK(cod4_handshake_init(&sv, 1, "has space") == 0);
    CHECK(cod4_handshake_init(&sv, 1, ""));
    in[0] = in[1] = in[2] = in[3] = 1;
    n = cod4_handshake_packet(&sv, 1, 2, in, 4, out, 128);
    CHECK(n == 0);
    n = cod4_handshake_packet(&sv, 1, 2, in, packet(in, "getinfo"), out, 128);
    CHECK(n == 0);
    n = cod4_handshake_packet(&sv, 1, 2, in, packet(in, "getchallenge"), out, 128);
    CHECK(same(out, n, "challengeResponse 1"));
    n = cod4_handshake_packet(&sv, 1, 2, in,
                              packet(in, "connect \"\\protocol\\6\\challenge\\9\\qport\\1\""), out, 128);
    CHECK(same(out, n, "error\nEXE_BAD_CHALLENGE"));
    n = cod4_handshake_packet(&sv, 1, 2, in,
                              packet(in, "connect \"\\protocol\\6\\challenge\\1\\qport\\65536\""), out, 128);
    CHECK(same(out, n, "error\nEXE_BAD_CHALLENGE"));
}

int main(void) {
    test_challenge_and_connect();
    test_version_and_full();
    test_rejects();
    if (g_fails) {
        fprintf(stderr, "%d failed\n", g_fails);
        return 1;
    }
    printf("cod4 handshake ok\n");
    return 0;
}
