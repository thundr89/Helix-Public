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

enum { GSC_PC_SOUND = 1, GSC_PC_MODEL, GSC_PC_SHADER, GSC_PC_WEAPON, GSC_PC_FX };

/* Borrowed strings are valid for the synchronous call only. No VM pointers cross
 * the host boundary. Native status: 1=handled, 0=unknown, -1=rejected. */
enum { GSC_NATIVE_UNDEFINED, GSC_NATIVE_INT, GSC_NATIVE_FLOAT,
       GSC_NATIVE_STRING, GSC_NATIVE_ENTITY, GSC_NATIVE_VECTOR, GSC_NATIVE_RECORD,
       GSC_NATIVE_ANIMATION, GSC_NATIVE_ANIMTREE };
typedef struct GscNativeField GscNativeField;
typedef struct GscNativeValue {
    int kind, integer;
    double number, vector[3];
    const char* text;
    const GscNativeField* fields; int field_count;
    const char* tree; /* Animation values retain their owning animtree name. */
} GscNativeValue;
struct GscNativeField { const char* name; GscNativeValue value; };
typedef struct GscHost {
    void* user;
    int (*read_file)(void* user, const char* path, char** text, size_t* size);
    void (*ambient)(void* user, const char* name);
    void (*give_weapon)(void* user, const char* name);
    void (*take_weapons)(void* user);
    void (*set_team_names)(void* user, const char* allies, const char* axis);
    void (*set_gametype)(void* user, const char* name);
    void (*precache)(void* user, int kind, const char* name);
    void (*set_cvar)(void* user, const char* name, const char* value);
    const char* (*get_cvar)(void* user, const char* name);
    int (*entity_find)(void* user, const char* key, const char* value, int* ids, int capacity);
    const char* (*entity_key)(void* user, int id, const char* key);
    int (*entity_state)(void* user, int id, int visible, int linked);
    void (*error)(void* user, const char* message);
    int (*place_spawnpoint)(void* user, int id);

    int (*native_call)(void* user, const char* name, int has_receiver, int receiver,
                       const GscNativeValue* args, int argc, GscNativeValue* result);
    /* value=NULL reads, otherwise writes a native property. Same status contract. */
    int (*native_field)(void*, int entity, const char* name,
                        const GscNativeValue* value, GscNativeValue* result);
    int (*sound_alias_exists)(void*,const char*);
    void (*animation_reference)(void*,const char* tree,const char* clip);
} GscHost;

typedef struct GscRuntime GscRuntime;
typedef struct GscArgument { int kind; int integer; double number; const char* text; double vector[3]; } GscArgument;
/* Argument kinds: 1=int, 2=float, 3=string, 4=entity id, 5=vector. Entity -1 is level, -2 is game. */
GscRuntime* gsc_runtime_create(const GscHost* host);
void gsc_runtime_destroy(GscRuntime* runtime);
int gsc_runtime_start(GscRuntime* runtime, const char* path, const char* source, size_t size, const char* function, int self_entity);
/* Request the native startup wrapper once, after all main roots finish. */
int gsc_runtime_start_gametype(GscRuntime* runtime);
/* Dispatch the current level function reference. Does not load files or recurse
 * into the scheduler. Arguments are copied before returning. */
int gsc_runtime_call_level(GscRuntime*,const char* field,int self,const GscArgument*,int argc);
int gsc_runtime_get_entity_value(GscRuntime*,int entity,const char* field,const char* key,GscArgument*);
/* Copied entity ids for plugin-side AI; dynamic script fields participate. */
int gsc_runtime_find_entities(GscRuntime*,const char* key,const char* value,int* ids,int capacity);
int gsc_runtime_get_entity_index(GscRuntime*,int entity,const char* field,int index,GscArgument*);
void gsc_runtime_forget_entity(GscRuntime*,int entity);
/* Freeze native fields and detach from the reusable host slot. Existing threads
 * retain their object; a future connection receives a different object. */
void gsc_runtime_cancel_entity_threads(GscRuntime*,int entity);
int gsc_runtime_retire_entity(GscRuntime*,int entity);
int gsc_runtime_entity_busy(const GscRuntime*,int retired_entity);
int gsc_runtime_reset_map(GscRuntime*);
int gsc_runtime_tick(GscRuntime* runtime, int time_ms, int instruction_budget);
/* Host-owned player state enters the same persistent objects scripts read.
 * key=NULL writes a field; otherwise updates a string key in an array field. */
int gsc_runtime_set_entity_value(GscRuntime*,int entity,const char* field,const char* key,const GscArgument* value);
int gsc_runtime_notify(GscRuntime* runtime, int entity, const char* event, const GscArgument* args, int argc);
int gsc_runtime_threads(const GscRuntime* runtime);
int gsc_runtime_errors(const GscRuntime* runtime);
const char* gsc_runtime_level_function(const GscRuntime* runtime, const char* field);
int gsc_runtime_start_dispatched(const GscRuntime* runtime);
int gsc_runtime_start_complete(const GscRuntime*);
int gsc_runtime_loading_pending(const GscRuntime*);
int gsc_runtime_is_waiting(const GscRuntime*,int entity,const char* event);
void gsc_runtime_precache_dependencies(GscRuntime*);
int gsc_runtime_level_int(const GscRuntime* runtime, const char* field, int* value);
const char* gsc_runtime_game_string(const GscRuntime* runtime,const char* field);

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
