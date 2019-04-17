#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

// Returns the aproximated base 10 length of an integer.
// This macro returns a constant upper bound of the length,
// as to permit static buffer allocation without worry of
// overflow.
#define MAX_STRLEN_FOR_INT_TYPE(t) \
	(((t) -1 < 0) ? __MAX_STRLEN_FOR_SIGNED_TYPE(t) \
				  : __MAX_STRLEN_FOR_UNSIGNED_TYPE(t))

#define __MAX_STRLEN_FOR_UNSIGNED_TYPE(t) \
	(((((sizeof(t) * CHAR_BIT)) * 1233) >> 12) + 1)

#define __MAX_STRLEN_FOR_SIGNED_TYPE(t) \
	(__MAX_STRLEN_FOR_UNSIGNED_TYPE(t) + 1)

#ifndef NDEBUG

	#define UNREACHABLE() do { \
		fprintf(stderr, "%s[%d]@%s(): reached unreachable code.\n", \
		    __FILE__, __LINE__, __func__); \
		abort(); \
	} while(0) \

	#define assert(cond, msg) do { \
		if(!(cond)) { \
			fprintf(stderr, "Assertion failed: %s\n", msg); \
			abort(); \
		} \
	} while(0)

#else

	#define UNREACHABLE()
	#define assert(cond, msg)

#endif

// Returns the closest power of two to n, be it 2^x, where 2^x >= n
int powerOf2Ceil(int n);

// Hash a string
static inline uint32_t hashString(const char *str, size_t length) {
	uint32_t hash = 2166136261u;

	for (size_t i = 0; i < length; i++) {
		hash ^= str[i];
		hash *= 16777619;
	}

	return hash;
}

#endif
