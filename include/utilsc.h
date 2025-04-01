#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static const char digits[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@!#$%^&*()-_=+[]{}|;:'\",.<>?/`~";

#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))
#define BENCHMARK(code)                                                                                                \
    {                                                                                                                  \
        float startTime = (float)clock() / CLOCKS_PER_SEC;                                                             \
        code;                                                                                                          \
        float endTime = (float)clock() / CLOCKS_PER_SEC;                                                               \
        float timeElapsed = endTime - startTime;                                                                       \
        printf("============\n Benchmark: %s \n %f s.\n============\n", #code, timeElapsed);                           \
    }

bool intToStr(char *buf, int n, int base);
void reverseStr(char *cstr, size_t strLength);
size_t removeChar(char toRemove, char *cstr, size_t strLength);

inline bool intToStr(char *buf, int n, int base) {
    const size_t maxValidBase = LENGTH(digits);
    if (base < 2 || base > maxValidBase) {
        printf("only base 2-%zu are supported", maxValidBase);
        return false;
    }
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return true;
    }
    size_t bufIndex = 0;
    if (n < 0) {
        buf[bufIndex++] = '-';
    }
    n = abs(n);
    do {
        buf[bufIndex++] = digits[n % base];
    } while (n /= base);
    reverseStr(buf, bufIndex);
    buf[bufIndex] = '\0';
    return true;
}

inline void reverseStr(char *cstr, size_t strLength) {
    for (size_t l = 0; l < strLength / 2; l++) {
        size_t r = strLength - l - 1;
        char tmp = cstr[l];
        cstr[l] = cstr[r];
        cstr[r] = tmp;
    }
}
inline size_t removeChar(char toRemove, char *cstr, size_t strLength) {
    size_t removed = 0;

    for (size_t l = 0, r = 0; r < strLength; r++) {
        if (cstr[r] != toRemove) {
            cstr[l] = cstr[r];
            l++;
        } else {
            removed++;
        }
    }

    return removed;
}

static const size_t da_init_capacity = 8;

typedef struct {
    char *data;
    size_t count;
} StringView;

#define daResize(da, new_count)                                                                                        \
    do {                                                                                                               \
        if ((new_count) > (da)->capacity) {                                                                            \
            size_t new_capacity = (da)->capacity;                                                                      \
            if ((da)->capacity == 0) {                                                                                 \
                new_capacity = da_init_capacity;                                                                       \
            }                                                                                                          \
            while (new_capacity < (new_count)) {                                                                       \
                new_capacity *= 2;                                                                                     \
            }                                                                                                          \
            void *new_items = realloc((void *)(da)->items, new_capacity * sizeof(*(da)->items));                       \
            if (new_items == NULL) {                                                                                   \
                assert(new_items != NULL && "Error during DA realloc");                                                \
            }                                                                                                          \
            (da)->capacity = new_capacity;                                                                             \
            (da)->items = (typeof((da)->items))new_items;                                                              \
        }                                                                                                              \
        (da)->count = new_count;                                                                                       \
    } while (false);

#define daAppend(da, item)                                                                                             \
    do {                                                                                                               \
        daResize((da), (da)->count + 1);                                                                               \
        (da)->items[(da)->count - 1] = (item);                                                                         \
    } while (false);


#define daRemoveAt(da, index)                                                                                          \
    do {                                                                                                               \
        if ((da)->count <= 0 || (da)->count <= (index)) {                                                              \
            fprintf(stderr, "Index %zu is out of bounds(da->count=%zu)\n", index, (da)->count);                         \
            break;                                                                                                     \
        }                                                                                                              \
        for (size_t i = index; i < (da)->count - 1; i++) {                                                             \
            (da)->items[i] = (da)->items[i + 1];                                                                       \
        }                                                                                                              \
        daResize((da), (da)->count - 1);                                                                               \
    } while (false);

#define daFree(da)                                                                                                     \
    do {                                                                                                               \
        free((void *)(da)->items);                                                                                     \
        (da)->count = 0;                                                                                               \
        (da)->capacity = 0;                                                                                            \
    } while (false)

#ifdef __cplusplus
}
#endif
