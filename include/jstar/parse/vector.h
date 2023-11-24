#ifndef JSTAR_VECTOR_H
#define JSTAR_VECTOR_H

#include "../conf.h"

#include <stdlib.h>
#include <string.h>

// Utility macro for iterating in a foreach style
#define ext_vec_foreach(elem, vec)                                                              \
    for(size_t __cont = 1, __i = 0; __cont && __i < ext_vec_size(vec); __cont = !__cont, __i++) \
        for(elem = ext_vec_iterator(vec, __i); __cont; __cont = !__cont)

// -----------------------------------------------------------------------------
// ALLOCATION
// -----------------------------------------------------------------------------

// Utility macro for declaring a vector
#define ext_vector(T) T*

// Release vector resources
#define ext_vec_free(vec)                   \
    do {                                    \
        if(vec) free(ext_vec_header_(vec)); \
    } while(0)

// -----------------------------------------------------------------------------
// CAPACITY
// -----------------------------------------------------------------------------

#define ext_vec_size(vec)     ((vec) ? (ext_vec_header_(vec)->size) : (size_t)0)
#define ext_vec_capacity(vec) ((vec) ? (ext_vec_header_(vec)->capacity) : (size_t)0)
#define ext_vec_empty(vec)    (ext_vec_size(vec) == 0)

// -----------------------------------------------------------------------------
// ELEMENT ACCESS
// -----------------------------------------------------------------------------

#define ext_vec_front(vec) ((vec)[0])
#define ext_vec_back(vec)  ((vec)[ext_vec_size(vec) - 1])

// -----------------------------------------------------------------------------
// ITERATORS
// -----------------------------------------------------------------------------

#define ext_vec_begin(vec) (vec)
#define ext_vec_end(vec)   ((vec) ? (vec) + ext_vec_header_(vec)->size : NULL)

#define ext_vec_iterator(vec, i)        ((vec) + i)
#define ext_vec_iterator_index(vec, it) (it - (vec))

// -----------------------------------------------------------------------------
// MODIFIERS
// -----------------------------------------------------------------------------

#define ext_vec_push_back(vec, e)         \
    do {                                  \
        ext_vec_maybe_grow_(vec, 1);      \
        size_t size = ext_vec_size(vec);  \
        (vec)[size] = (e);                \
        ext_vec_set_size_(vec, size + 1); \
    } while(0)

#define ext_vec_push_back_all(vec, arr, size)           \
    do {                                                \
        ext_vec_reserve(vec, ext_vec_size(vec) + size); \
        for(size_t i = 0; i < size; i++) {              \
            ext_vec_push_back(vec, arr[i]);             \
        }                                               \
    } while(0)

#define ext_vec_pop_back(vec)                                                  \
    do {                                                                       \
        JSR_ASSERT(ext_vec_size(vec) != 0, "Cannot pop_back on empty vector"); \
        ext_vec_set_size_(vec, ext_vec_size(vec) - 1);                         \
    } while(0)

#define ext_vec_insert(vec, i, e)                    \
    do {                                             \
        ext_vec_maybe_grow_(vec, 1);                 \
        size_t size = ext_vec_size(vec);             \
        JSR_ASSERT(i < size + 1, "Buffer overflow"); \
        size_t shift = (size - i) * sizeof(*(vec));  \
        memmove((vec) + i + 1, (vec) + i, shift);    \
        (vec)[i] = (e);                              \
        ext_vec_set_size_(vec, size + 1);            \
    } while(0)

#define ext_vec_erase(vec, i)                           \
    do {                                                \
        size_t size = ext_vec_size(vec);                \
        JSR_ASSERT(i < size, "Buffer overflow");        \
        size_t shift = (size - i - 1) * sizeof(*(vec)); \
        memmove((vec) + i, (vec) + i + 1, shift);       \
        ext_vec_set_size_(vec, size - 1);               \
    } while(0);

#define ext_vec_clear(vec)                      \
    do {                                        \
        if(vec) ext_vec_header_(vec)->size = 0; \
    } while(0)

#define ext_vec_reserve(vec, amount)                                                    \
    do {                                                                                \
        if(!(vec)) {                                                                    \
            vec_header_t* header = malloc(sizeof(*header) + (amount) * sizeof(*(vec))); \
            JSR_ASSERT(header, "Out of memory");                                        \
            header->capacity = (amount);                                                \
            header->size = 0;                                                           \
            (vec) = (void*)(header->data);                                              \
        } else if(ext_vec_capacity(vec) < (amount)) {                                   \
            vec_header_t* header = ext_vec_header_(vec);                                \
            header = realloc(header, sizeof(*header) + (amount) * sizeof(*(vec)));      \
            JSR_ASSERT(header, "Out of memory");                                        \
            header->capacity = (amount);                                                \
            (vec) = (void*)(header->data);                                              \
        }                                                                               \
    } while(0)

#define ext_vec_resize(vec, new_size, elem)           \
    do {                                              \
        size_t size = ext_vec_size(vec);              \
        if(new_size < size) {                         \
            ext_vec_set_size_(vec, new_size);         \
        } else {                                      \
            ext_vec_reserve(vec, new_size);           \
            for(size_t i = size; i < new_size; i++) { \
                (vec)[i] = elem;                      \
            }                                         \
            ext_vec_set_size_(vec, new_size);         \
        }                                             \
    } while(0)

#define ext_vec_shrink_to_fit(vec)                                                         \
    do {                                                                                   \
        if(vec) {                                                                          \
            vec_header_t* header = ext_vec_header_(vec);                                   \
            if(header->size) {                                                             \
                header = realloc(header, sizeof(*header) + sizeof(*(vec)) * header->size); \
                JSR_ASSERT(header, "Out of memory");                                       \
                header->capacity = header->size;                                           \
                (vec) = (void*)(header->data);                                             \
            } else {                                                                       \
                free(header);                                                              \
                (vec) = NULL;                                                              \
            }                                                                              \
        }                                                                                  \
    } while(0)

// -----------------------------------------------------------------------------
// PRIVATE - DON'T USE DIRECTLY
// -----------------------------------------------------------------------------

typedef struct {
    size_t capacity, size;  // Capacity (allocated memory) and size (slots used in the vector)
    char data[];            // The actual start of the vector memory
} vec_header_t;

#define ext_vec_maybe_grow_(vec, amount)                             \
    do {                                                             \
        size_t capacity = ext_vec_capacity(vec);                     \
        size_t size = ext_vec_size(vec);                             \
        if(size + (amount) > capacity) {                             \
            size_t new_capacity = capacity ? capacity * 2 : 1;       \
            while(size + (amount) > new_capacity) new_capacity *= 2; \
            ext_vec_reserve(vec, new_capacity);                      \
        }                                                            \
    } while(0)

#define ext_vec_header_(vec)            ((vec_header_t*)((char*)(vec) - sizeof(vec_header_t)))
#define ext_vec_set_capacity_(vec, cap) (ext_vec_header_(vec)->capacity = cap)
#define ext_vec_set_size_(vec, sz)      (ext_vec_header_(vec)->size = sz)

#endif
