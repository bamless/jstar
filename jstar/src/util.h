#ifndef UTIL_H
#define UTIL_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// Macros for defining an enum with associated string names using X macros
#define DEFINE_ENUM(NAME, ENUMX) typedef enum NAME { ENUMX(ENUM_ENTRY) } NAME
#define ENUM_ENTRY(ENTRY)        ENTRY,

#define DECLARE_ENUM_STRINGS(NAME)       extern const char* CONCATOK(NAME, Name)[]
#define DEFINE_ENUM_STRINGS(NAME, ENUMX) const char* CONCATOK(NAME, Name)[] = {ENUMX(STRINGIFY)}

#define STRINGIFY(X)   #X,
#define CONCATOK(X, Y) X##Y

// Returns the aproximated base 10 length of an integer. This macro returns a constant upper bound
// of the length, as to permit static buffer allocation without worry of overflow.
#define STRLEN_FOR_INT_TYPE(t) \
    (((t)-1 < 0) ? STRLEN_FOR_SIGNED_TYPE(t) : STRLEN_FOR_UNSIGNED_TYPE(t))

#define STRLEN_FOR_UNSIGNED_TYPE(t) (((((sizeof(t) * CHAR_BIT)) * 1233) >> 12) + 1)
#define STRLEN_FOR_SIGNED_TYPE(t)   (STRLEN_FOR_UNSIGNED_TYPE(t) + 1)

#ifndef NDEBUG

    #include <stdio.h>

    #define ASSERT(cond, msg)                                                              \
        do {                                                                               \
            if(!(cond)) {                                                                  \
                fprintf(stderr, "%s[%d]@%s(): assertion failed: %s\n", __FILE__, __LINE__, \
                        __func__, msg);                                                    \
                abort();                                                                   \
            }                                                                              \
        } while(0)

    #define UNREACHABLE()                                                                   \
        do {                                                                                \
            fprintf(stderr, "%s[%d]@%s(): reached unreachable code.\n", __FILE__, __LINE__, \
                    __func__);                                                              \
            abort();                                                                        \
        } while(0)

#else

    #define ASSERT(cond, msg)
    #define UNREACHABLE()

#endif

// Returns the closest power of two to n, be it 2^x, where 2^x >= n
static inline int powerOf2Ceil(int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

// Hash a string
static inline uint32_t hashString(const char* str, size_t length) {
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < length; i++) {
        hash ^= str[i];
        hash *= 16777619;
    }
    return hash;
}

#endif
