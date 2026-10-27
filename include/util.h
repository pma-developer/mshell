#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
static const char digits[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@!#$%^&*()-_=+[]{}|;:'\",.<>?/`~";

#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))

#if ENABLE_LOGS
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif // ENABLE_LOGS

#define BENCHMARK(code)                                                                                                \
    {                                                                                                                  \
        float startTime = (float)clock() / CLOCKS_PER_SEC;                                                             \
        code;                                                                                                          \
        float endTime = (float)clock() / CLOCKS_PER_SEC;                                                               \
        float timeElapsed = endTime - startTime;                                                                       \
        LOG("============\n Benchmark: %s \n %f s.\n============\n", #code, timeElapsed);                              \
    }

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StrVec;

typedef struct {
    StrVec *items;
    size_t count;
    size_t capacity;
} StrArrayVec;

bool intToStr(char *buf, int n, int base);
void reverseStr(char *cstr, size_t strLength);
size_t removeChar(char toRemove, char *cstr, size_t strLength);
char *cloneStr(const char *src);

