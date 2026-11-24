/*
 * CoD4 1.7 connectionless handshake.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "cod4_handshake.h"

#include <string.h>

static int game_ok(const char* game, char* out) {
    int n = 0;
    if (!game) game = "";
    while (game[n]) {
        char c = game[n];
        if (n >= 63 || c == ' ' || c == '"' || c == '\\' || c == '\n' || c == '\r')
            return 0;
        out[n++] = c;
    }
    out[n] = 0;
    return 1;
}

int cod4_handshake_init(Cod4Handshake* sv, int slots_free, const char* game) {
    if (!sv || slots_free < 0) return 0;
    memset(sv, 0, sizeof(*sv));
    sv->next_challenge = 1;
    sv->slots_free = slots_free;
    return game_ok(game, sv->game);
}

static Cod4Peer* peer_slot(Cod4Handshake* sv, uint32_t ip, uint16_t port) {
    int i;
    int free_slot = -1;
    for (i = 0; i < COD4_HANDSHAKE_PEERS; ++i) {
        Cod4Peer* p = &sv->peers[i];
        if ((p->live || p->connected) && p->ip == ip && p->port == port) return p;
        if (free_slot < 0 && !p->live) free_slot = i;
    }
    if (free_slot < 0) free_slot = 0;
    sv->peers[free_slot].ip = ip;
    sv->peers[free_slot].port = port;
    sv->peers[free_slot].live = 0;
    sv->peers[free_slot].connected = 0;
    return &sv->peers[free_slot];
}

static const Cod4Peer* find_peer(const Cod4Handshake* sv, uint32_t ip, uint16_t port) {
    int i;
    for (i = 0; i < COD4_HANDSHAKE_PEERS; ++i) {
        const Cod4Peer* p = &sv->peers[i];
        if (p->ip == ip && p->port == port && (p->live || p->connected)) return p;
    }
    return 0;
}

int cod4_handshake_qport(const Cod4Handshake* sv, uint32_t ip, uint16_t port) {
    const Cod4Peer* p = sv ? find_peer(sv, ip, port) : 0;
    if (!p || !p->connected) return -1;
    return (int)p->qport;
}

static int oob_text(const unsigned char* packet, int len, char* text, int cap) {
    int n;
    int i;
    if (len < 4) return 0;
    if (packet[0] != 0xff || packet[1] != 0xff || packet[2] != 0xff || packet[3] != 0xff)
        return 0;
    n = len - 4;
    if (n >= cap) n = cap - 1;
    for (i = 0; i < n; ++i) {
        if (packet[4 + i] == 0) break;
        text[i] = (char)packet[4 + i];
    }
    text[i] = 0;
    return 1;
}

static int reply_text(unsigned char* reply, int cap, const char* text) {
    int n = (int)strlen(text);
    int i;
    if (n < 0 || n > cap - 4) return -1;
    reply[0] = reply[1] = reply[2] = reply[3] = 0xff;
    for (i = 0; i < n; ++i) reply[4 + i] = (unsigned char)text[i];
    return 4 + n;
}

static const char* token_end(const char* s) {
    while (*s && *s != ' ' && *s != '\t') ++s;
    return s;
}

static int decimal(const char* s, int* out) {
    int v = 0;
    int i = 0;
    if (!s || s[0] < '0' || s[0] > '9') return 0;
    while (s[i] >= '0' && s[i] <= '9') {
        int digit = s[i] - '0';
        if (v > (2147483647 - digit) / 10) return 0;
        v = v * 10 + digit;
        if (++i > 10) return 0;
    }
    if (s[i] != 0) return 0;
    *out = v;
    return 1;
}

static int info_value(const char* info, const char* key, char* out, int cap) {
    int key_len = (int)strlen(key);
    const char* p = info;
    out[0] = 0;
    while (*p) {
        const char* start;
        int n;
        if (*p != '\\') return 0;
        ++p;
        start = p;
        while (*p && *p != '\\') ++p;
        n = (int)(p - start);
        if (*p != '\\') return 0;
        ++p;
        if (n == key_len && memcmp(start, key, (size_t)n) == 0) {
            int i = 0;
            while (*p && *p != '\\') {
                if (i >= cap - 1) return 0;
                out[i++] = *p++;
            }
            out[i] = 0;
            return 1;
        }
        while (*p && *p != '\\') ++p;
    }
    return 0;
}

static int version_error(unsigned char* reply, int cap) {
    char text[64];
    int n = 0;
    const char* head = "error\nEXE_SERVER_IS_DIFFERENT_VER";
    const char* tail = "1.7\n";
    while (*head) text[n++] = *head++;
    text[n++] = 0x15;
    while (*tail) text[n++] = *tail++;
    text[n] = 0;
    return reply_text(reply, cap, text);
}

int cod4_handshake_packet(Cod4Handshake* sv, uint32_t ip, uint16_t port,
                          const void* packet, int len,
                          unsigned char* reply, int reply_cap) {
    char text[1024];
    const char* cmd;
    const char* rest;
    char command[32];
    int cmd_len;
    if (!sv || !packet || len < 0 || !reply || reply_cap < 64) return -1;
    if (!oob_text((const unsigned char*)packet, len, text, (int)sizeof(text))) return 0;
    cmd = text;
    while (*cmd == ' ' || *cmd == '\t') ++cmd;
    rest = token_end(cmd);
    cmd_len = (int)(rest - cmd);
    if (cmd_len <= 0 || cmd_len >= (int)sizeof(command)) return 0;
    memcpy(command, cmd, (size_t)cmd_len);
    command[cmd_len] = 0;
    while (*rest == ' ' || *rest == '\t') ++rest;

    if (strcmp(command, "getchallenge") == 0) {
        Cod4Peer* peer = peer_slot(sv, ip, port);
        char body[40];
        int challenge = sv->next_challenge++;
        int n;
        if (sv->next_challenge < 1) sv->next_challenge = 1;
        peer->ip = ip;
        peer->port = port;
        peer->challenge = challenge;
        peer->live = 1;
        peer->connected = 0;
        n = 0;
        {
            const char* head = "challengeResponse ";
            char num[16];
            int v = challenge;
            int k = 0;
            if (v == 0) num[k++] = '0';
            else {
                char rev[16];
                int r = 0;
                while (v) {
                    rev[r++] = (char)('0' + (v % 10));
                    v /= 10;
                }
                while (r) num[k++] = rev[--r];
            }
            num[k] = 0;
            while (*head) body[n++] = *head++;
            for (v = 0; v < k; ++v) body[n++] = num[v];
            body[n] = 0;
        }
        return reply_text(reply, reply_cap, body);
    }

    if (strcmp(command, "connect") == 0) {
        char info[1024];
        char value[32];
        int protocol = 0;
        int challenge = 0;
        int qport = 0;
        Cod4Peer* peer;
        const char* body = rest;
        int i = 0;
        if (*body == '"') {
            ++body;
            while (body[i] && body[i] != '"') {
                if (i >= (int)sizeof(info) - 1) return 0;
                info[i] = body[i];
                ++i;
            }
            info[i] = 0;
        } else {
            while (body[i]) {
                if (i >= (int)sizeof(info) - 1) return 0;
                info[i] = body[i];
                ++i;
            }
            info[i] = 0;
        }
        if (!info_value(info, "protocol", value, (int)sizeof(value)) || !decimal(value, &protocol) ||
            protocol != COD4_PROTOCOL)
            return version_error(reply, reply_cap);
        if (!info_value(info, "challenge", value, (int)sizeof(value)) || !decimal(value, &challenge))
            return reply_text(reply, reply_cap, "error\nEXE_BAD_CHALLENGE");
        if (!info_value(info, "qport", value, (int)sizeof(value)) || !decimal(value, &qport) || qport > 65535)
            return reply_text(reply, reply_cap, "error\nEXE_BAD_CHALLENGE");
        peer = peer_slot(sv, ip, port);
        if (!peer->live || peer->challenge != challenge)
            return reply_text(reply, reply_cap, "error\nEXE_BAD_CHALLENGE");
        if (sv->slots_free <= 0)
            return reply_text(reply, reply_cap, "error\nEXE_SERVERISFULL");
        peer->live = 0;
        peer->connected = 1;
        peer->qport = (uint16_t)qport;
        sv->slots_free -= 1;
        {
            char line[96];
            int n = 0;
            const char* head = "connectResponse ";
            const char* game = sv->game;
            while (*head) line[n++] = *head++;
            while (*game) line[n++] = *game++;
            line[n] = 0;
            return reply_text(reply, reply_cap, line);
        }
    }
    return 0;
}
