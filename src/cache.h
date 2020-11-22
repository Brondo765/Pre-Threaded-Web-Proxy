#pragma once

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (512 * 1024)

/* Recommended: write the cache functions in cache.c, add cache.c to SOURCES in
 * src/Makefile, and implement the following functions:
 *
 * void cache_init ():
 * Initialize the cache dictionary, defined as a global variable in cache.c.
 *
 * int cache_write_if_cached (const char *uri, int fd):
 * If uri is found in the cache, write it to fd and return 0, otherwise return 1.
 *
 * void cache_add_to_cache (const char *uri, const char *buf, size_t buf_len):
 * Add the pair (uri, buf) to the cache, possibly evicting old cached objects.
 */
