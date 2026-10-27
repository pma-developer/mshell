#include "util.h"

#include "vec.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void freeStrArrayVec(StrArrayVec *vec) {
    for (int i = 0; i < vec->count; ++i) {
        vecFree(&vec->items[i]);
    }
    vecFree(vec);
}

char *cloneStr(const char *src) {
    size_t len = strlen(src) + 1;
    char *copy = malloc(sizeof(char) * len);
    if (copy == NULL) {
        perror("malloc in cloneStr");
        return NULL;
    }
    strcpy(copy, src);
    return copy;
}

bool intToStr(char *buf, int n, int base) {
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

void reverseStr(char *cstr, size_t strLength) {
    for (size_t l = 0; l < strLength / 2; l++) {
        size_t r = strLength - l - 1;
        char tmp = cstr[l];
        cstr[l] = cstr[r];
        cstr[r] = tmp;
    }
}
size_t removeChar(char toRemove, char *str, size_t strLength) {
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