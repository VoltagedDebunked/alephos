#ifndef STR_H
#define STR_H

#include <utils/mem.h>

static int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

static int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static char* strdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* new_str = malloc(len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    return new_str;
}

static const char* strchr(const char* str, int c) {
    while (*str != '\0') {
        if (*str == (char)c) {
            return str;
        }
        str++;
    }
    return NULL;
}

static const char* strstr(const char* haystack, const char* needle) {
    if (*needle == '\0') {
        return haystack;
    }

    while (*haystack != '\0') {
        const char* h = haystack;
        const char* n = needle;

        while (*h != '\0' && *n != '\0' && *h == *n) {
            h++;
            n++;
        }

        if (*n == '\0') {
            return haystack;
        }

        haystack++;
    }

    return NULL;
}

#endif