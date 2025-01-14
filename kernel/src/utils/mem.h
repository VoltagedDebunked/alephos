#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <stddef.h>

// Memory functions
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

// Memory allocation functions
void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);

#endif // MEM_H