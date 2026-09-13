/*
 * Helix Engine — IAssetPlugin public SDK interface
 *
 * Vendored ABI snapshot (API version 1) from the Helix SDK.
 * This header is the ONLY Helix interface GPL adapters may include.
 *
 * ABI contract:
 *   - Shared library exports ONE C function:
 *       hxAssetPlugin* helix_asset_plugin_create(void);
 *   - The engine calls destroy() when done; the plugin frees itself.
 *   - api_version MUST equal HELIX_ASSET_PLUGIN_API_VERSION; the engine
 *     refuses to load plugins with a mismatched version.
 *
 * C linkage is used throughout so that plugins may be compiled with any
 * compiler/runtime combination without C++ ABI concerns.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define HELIX_ASSET_PLUGIN_API_VERSION 1

/**
 * hxAssetPlugin
 *
 * Vtable-style struct.  All function pointers must be non-NULL.
 * 'self' is always the hxAssetPlugin* itself (C equivalent of 'this').
 */
typedef struct hxAssetPlugin {
    /** Must equal HELIX_ASSET_PLUGIN_API_VERSION. */
    int api_version;

    /**
     * Human-readable format tag: "wad", "pak", "pk3", "pk4", ...
     * Must be a static string; never freed by the engine.
     */
    const char* format_name;

    /**
     * open() -- Open an archive file at the given filesystem path.
     * Returns 1 on success, 0 on failure.
     * May be called multiple times; each call replaces the previous archive.
     */
    int (*open)(struct hxAssetPlugin* self, const char* path);

    /**
     * contains() -- Returns 1 if the archive contains a file with the
     * given virtual path (forward-slash separated, no leading slash).
     */
    int (*contains)(struct hxAssetPlugin* self, const char* name);

    /**
     * read() -- Decompress/extract the named file into a newly allocated
     * buffer.  On success, writes a heap-allocated buffer to *out_buf and
     * its byte length to *out_size, then returns 1.
     * On failure returns 0 and leaves *out_buf / *out_size untouched.
     * The caller MUST free the buffer via free_buf().
     */
    int (*read)(struct hxAssetPlugin* self,
                const char*           name,
                unsigned char**       out_buf,
                unsigned int*         out_size);

    /**
     * free_buf() -- Release a buffer previously returned by read().
     */
    void (*free_buf)(struct hxAssetPlugin* self, unsigned char* buf);

    /**
     * list() -- Enumerate all virtual paths in the open archive.
     * Writes a heap-allocated array of heap-allocated C strings to *names
     * and the count to *count.
     * The caller MUST free the result via free_list().
     */
    void (*list)(struct hxAssetPlugin* self,
                 char***               names,
                 unsigned int*         count);

    /**
     * free_list() -- Release the array returned by list().
     */
    void (*free_list)(struct hxAssetPlugin* self,
                      char**               names,
                      unsigned int         count);

    /**
     * close() -- Close the currently open archive and release archive-level
     * resources.  The plugin struct itself remains alive.
     */
    void (*close)(struct hxAssetPlugin* self);

    /**
     * destroy() -- Free the plugin struct and all associated resources.
     * Must be called by the engine after close().
     * After this call the pointer is invalid.
     */
    void (*destroy)(struct hxAssetPlugin* self);

} hxAssetPlugin;

/**
 * Every asset plugin shared library MUST export this symbol with C linkage:
 *
 *   hxAssetPlugin* helix_asset_plugin_create(void);
 *
 * The engine calls this once per DLL load.  The returned pointer is owned
 * by the plugin; the engine releases it by calling destroy().
 */
typedef hxAssetPlugin* (*hxAssetPluginCreateFn)(void);
#define HELIX_ASSET_PLUGIN_ENTRY "helix_asset_plugin_create"

#ifdef __cplusplus
} /* extern "C" */
#endif
