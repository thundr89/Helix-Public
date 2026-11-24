/* GPL-2.0-or-later. Opt-in, closed-link GSC runtime profile. */
#ifndef GSC_LINKED_H
#define GSC_LINKED_H
#include "gsc/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { GSC_BIND_FUNCTION=1, GSC_BIND_METHOD=2 };
typedef int (*GscNativeEntry)(void* user,int has_receiver,int receiver,
                            const GscNativeValue* args,int argc,GscNativeValue* result);
typedef struct GscNativeBinding {
    const char* name;
    unsigned flags;
    GscNativeEntry call;
    void* user;
} GscNativeBinding;
/* Copies the registry and its names. Function pointers/users must outlive runtime.
 * No fallback to host.native_call for bound external calls. Existing create()
 * behavior is unchanged. A failed linked build cannot subsequently execute. */
GscRuntime* gsc_runtime_create_linked(const GscHost*,const GscNativeBinding*,int count);
/* Preparation only: compile and recursively load dependencies; no script runs.
 * Dynamic local/field names remain legal. Add every root before link(). */
int gsc_runtime_add_source(GscRuntime*,const char* path,const char* source,size_t size);
/* Validate every required root before sealing/scheduling any root. A missing
 * entry poisons preparation, just like an unresolved dependency. */
int gsc_runtime_require_entry(GscRuntime*,const char* path,const char* function);
/* Seal once: resolve all script calls/references and native slots, including
 * uncalled functions. Missing or ambiguous symbols prevent all execution.
 * After success use gsc_runtime_start() on prepared entries, then tick().
 * No source loading or registry mutation is permitted after sealing. */
int gsc_runtime_link(GscRuntime*);
#ifdef __cplusplus
}
#endif
#endif
