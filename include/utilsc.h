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
#if ENABLE_LOGS
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...)
#endif // ENABLE_LOGS
#define BENCHMARK(code)                                                                                                \
    {                                                                                                                  \
        float startTime = (float)clock() / CLOCKS_PER_SEC;                                                             \
        code;                                                                                                          \
        float endTime = (float)clock() / CLOCKS_PER_SEC;                                                               \
        float timeElapsed = endTime - startTime;                                                                       \
        LOG("============\n Benchmark: %s \n %f s.\n============\n", #code, timeElapsed);                              \
    }

bool intToStr(char *buf, int n, int base);
void reverseStr(char *cstr, size_t strLength);
size_t removeChar(char toRemove, char *cstr, size_t strLength);

inline bool intToStr(char *buf, int n, int base) {
    const size_t maxValidBase = LENGTH(digits);
    if (base < 2 || base > maxValidBase) {
        LOG("only base 2-%zu are supported", maxValidBase);
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
inline size_t removeChar(char toRemove, char *str, size_t strLength) {
    size_t removed = 0;

    for (size_t l = 0, r = 0; r < strLength; r++) {
        if (str[r] != toRemove) {
            str[l] = str[r];
            l++;
        } else {
            removed++;
        }
    }

    return removed;
}

static const size_t vec_init_capacity = 8;

typedef struct {
    char *data;
    size_t count;
} StringView;

#define vecResize(vec, new_count)                                                                                        \
    do {                                                                                                               \
        if ((new_count) > (vec)->capacity) {                                                                            \
            size_t _new_capacity = (vec)->capacity;                                                                     \
            if ((vec)->capacity == 0) {                                                                                 \
                _new_capacity = vec_init_capacity;                                                                      \
            }                                                                                                          \
            while (_new_capacity < (new_count)) {                                                                      \
                _new_capacity *= 2;                                                                                    \
            }                                                                                                          \
            void *_new_items = realloc((void *)(vec)->items, _new_capacity * sizeof(*(vec)->items));                     \
            if (_new_items == NULL) {                                                                                  \
                assert(_new_items != NULL && "Error during vector realloc");                                               \
            }                                                                                                          \
            (vec)->capacity = _new_capacity;                                                                            \
            (vec)->items = (typeof((vec)->items))_new_items;                                                             \
        }                                                                                                              \
        (vec)->count = new_count;                                                                                       \
    } while (false);

#define vecAppend(vec, item)                                                                                             \
    do {                                                                                                               \
        vecResize((vec), (vec)->count + 1);                                                                               \
        (vec)->items[(vec)->count - 1] = (item);                                                                         \
    } while (false);

#define vecRemoveAt(vec, index)                                                                                          \
    do {                                                                                                               \
        if ((vec)->count <= 0 || (vec)->count <= (index)) {                                                              \
            LOG("Index %zu is out of bounds(vec->count=%zu)\n", index, (vec)->count);                                    \
            break;                                                                                                     \
        }                                                                                                              \
        for (size_t _i = index; _i < (vec)->count - 1; _i++) {                                                          \
            (vec)->items[_i] = (vec)->items[_i + 1];                                                                     \
        }                                                                                                              \
        vecResize((vec), (vec)->count - 1);                                                                               \
    } while (false);

#define vecFree(vec)                                                                                                     \
    do {                                                                                                               \
        if ((vec)->capacity > 0) {                                                                                      \
            free((void *)(vec)->items);                                                                              \
            (vec)->count = 0;                                                                                           \
            (vec)->items = NULL;                                                                                           \
            (vec)->capacity = 0;                                                                                        \
        }                                                                                                              \
    } while (false)

#ifdef __cplusplus
}
#endif
