// Tiny string helpers. The mod is compiled with -nostdinc, so there is no libc
// to call -- these are the handful of operations the mod actually needs.

#ifndef TWITCH_STR_H
#define TWITCH_STR_H

static inline int twitch_strlen(const char* s) {
    int n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static inline void twitch_strcpy(char* dst, const char* src, int capacity) {
    int i = 0;
    if (capacity <= 0) {
        return;
    }
    while (src[i] != '\0' && i < capacity - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static inline void twitch_strcat(char* dst, const char* src, int capacity) {
    int len = 0;
    while (dst[len] != '\0' && len < capacity - 1) {
        len++;
    }
    twitch_strcpy(dst + len, src, capacity - len);
}

static inline int twitch_streq(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static inline char twitch_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive compare of `a` against an all-lowercase literal `b`.
static inline int twitch_streq_lower(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (twitch_lower(*a) != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

#endif // TWITCH_STR_H
