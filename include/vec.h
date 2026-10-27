#pragma once
#include <stddef.h>

static const size_t vec_init_capacity = 8;
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
