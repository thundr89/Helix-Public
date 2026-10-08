/*
 * Small CoD GSC interpreter for map and gametype mains.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef GSC_VM_H
#define GSC_VM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GscHost {
    void* user;
    int (*read_file)(void* user, const char* path, char** text, size_t* size);
    void (*ambient)(void* user, const char* name);
    void (*give_weapon)(void* user, const char* name);
    void (*take_weapons)(void* user);
    void (*set_team_names)(void* user, const char* allies, const char* axis);
    void (*set_gametype)(void* user, const char* name);
} GscHost;

/* Runs `func` (usually "main") and records giveWeapon / ambient / team / gametype. */
int gsc_vm_exec(const char* source, size_t size, const char* func, GscHost* host);

/* Literal giveWeapon("name") scan, used when main does not call it directly. */
int gsc_vm_scan_weapons(const char* source, size_t size, char names[][64], int max_names);

/* Last integer value returned by the entry function. Zero when the return was
 * not an integer. Updated on every successful gsc_vm_exec. */
extern int gsc_vm_last_int;

#ifdef __cplusplus
}
#endif

#endif /* GSC_VM_H */
