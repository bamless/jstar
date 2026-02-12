/**
 * extlib v1.4.0 - c extended library
 *
 * Single-header-file library that provides functionality that extends the standard c library.
 * Features:
 *     - Context abstraction to easily modify the application's allocation and logging behaviour
 *     - Custom Allocators that easily integrate with the context and datastructures
 *     - Generic, type-safe dynamic arrays and hash maps
 *     - StringBuffer for easily working with dynamic buffers of bytes
 *     - StringSlice for easy manipulation of immutable 'views' over byte buffers
 *     - Cross-platform IO functions for working with processes, files and more generally
 *       interacting with the operating system
 *     - Configurable logging
 *     - Misc utility macros
 *     - No-std and wasm support
 *
 *  To use the library do this in *exactly one* c file:
 *  ```c
 *  #define EXTLIB_IMPL
 *  // Optional configuration options
 *  #include "extlib.h"
 *  ```
 *  Configuration options:
 *  ```c
 *  #define EXTLIB_NO_SHORTHANDS // Disable shorthands names, only prefixed one will be defined
 *  #define EXTLIB_NO_STD        // Do not use libc functions
 *  #define EXTLIB_WASM          // Enable when compiling for wasm target. Implies EXTLIB_NO_STD
 *  #define EXTLIB_THREADSAFE    // Thread safe Context
 *  #define NDEBUG               // Strips runtime assertions and replaces unreachables with
 *                               // compiler intrinsics
 *  ```
 *
 *  To get more information on specific components, grep for:
 *      SECTION: Context
 *      SECTION: Allocators
 *      SECTION: Temporary allocator
 *      SECTION: Arena allocator
 *      SECTION: Dynamic array
 *      SECTION: Hashmap
 *      SECTION: String buffer
 *      SECTION: String slice
 *      SECTION: IO
 *
 *  Changelog:
 *
 *  v1.4.0:
 *      - Simplified arena allocator
 *      - Fixes in temp allocator and arena allocator
 *
 *  v1.3.2:
 *      - Fix build under win32 clang
 *
 *  v1.3.1:
 *      - Bugfixes in path handling functions, especially around win32 drive letters and UCN paths
 *      - Fixed `ext_new_array` macro
 *      - Minor other bugfixes
 *
 *  v1.3.0:
 *      - New `StringSlice` functions: `ss_strip_prefix`, `ss_strip_suffix` (and `_cstr`
 *        variants), `ss_eq_ignore_case`, `ss_cmp_ignore_case`, `ss_starts_with_ignore_case`,
 *        `ss_ends_with_ignore_case` (and `_cstr` variants), `ss_substr`
 *      - New path manipulation functions: `ss_basename`, `ss_dirname`, `ss_extension`,
 *        `sb_append_path` (and `_cstr` variant). Handles both `/` and `\` on Windows
 *      - New `StringBuffer` functions: `sb_to_upper`, `sb_to_lower`, `sb_reverse`
 *      - Added missing shorthand aliases for `ss_foreach_split_cstr` and
 *        `ss_foreach_rsplit_cstr`
 *
 *  v1.2.1:
 *      - Added `arena_push` and `arena_pop` macros
 *      - Renamed `DEFER_LOOP` to `defer_loop`. Old version is mantained for backwards compatibility
 *      - Removed unused defines
 *
 *  v1.2.0:
 *      - Added `EXT_DEBUG` logging level
 *      - Minor `Ext_Arena` redesign
 *      - New `EXT_KiB`, `EXT_MiB`, `EXT_GiB` macros
 *
 *  v1.1.0:
 *      - Added generic allocator versions of convenience macros: `ext_allocator_new`,
 *        `ext_allocator_new_array`, `ext_allocator_delete`, `ext_allocator_delete_array`,
 *        and `ext_allocator_clone` (with corresponding shorthands when EXTLIB_NO_SHORTHANDS
 *        is not defined)
 *
 *  v1.0.2:
 *      - Added `DEFER_LOOP` macro
 */
#ifndef EXTLIB_H
#define EXTLIB_H

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef EXTLIB_WASM
#ifndef EXTLIB_NO_STD
#define EXTLIB_NO_STD
#endif  // EXTLIB_NO_STD

#ifdef EXTLIB_THREADSAFE
#undef EXTLIB_THREADSAFE
#endif  // EXTLIB_THREADSAFE
#endif  // EXTLIB_WASM

#if defined(_WIN32)
#define EXT_WINDOWS
#elif defined(__linux__)
#define EXT_LINUX
#define EXT_POSIX
#elif defined(__ANDROID__)
#define EXT_ANDROID
#define EXT_POSIX
#elif defined(__FreeBSD__)
#define EXT_FREEBSD
#define EXT_POSIX
#elif defined(__OpenBSD__)
#define EXT_OPENBSD
#define EXT_POSIX
#elif defined(__EMSCRIPTEN__)
#define EXT_EMSCRIPTEN
#elif defined(__APPLE__) || defined(__MACH__)
#include <TargetConditionals.h>

#if TARGET_OS_IPHONE == 1
#define EXT_IOS
#elif TARGET_OS_MAC == 1
#define EXT_MACOS
#endif

#define EXT_POSIX
#endif

#ifndef EXTLIB_NO_STD
#include <assert.h>
#include <errno.h>  // IWYU pragma: export
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else   // EXTLIB_NO_STD
static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for(size_t i = 0; i < n; i++) {
        if(p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}
static inline void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for(size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}
static inline void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while(n--) *p++ = (unsigned char)c;
    return s;
}
static inline size_t strlen(const char *s) {
    size_t len = 0;
    while(s[len] != '\0') len++;
    return len;
}
void assert(int c);  // TODO: are we sure we want to require wasm embedder to provide `assert`?
#endif  // EXTLIB_NO_STD

#ifdef EXTLIB_THREADSAFE
#if defined(_MSC_VER)
// MSVC supports __declspec(thread), but not for dynamically initialized data
#define EXT_TLS __declspec(thread)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_THREADS__)
#include <threads.h>
#define EXT_TLS thread_local
#elif defined(__GNUC__) || defined(__clang__)
// GCC and Clang (even with C99 or C89)
#define EXT_TLS __thread
#else
#warning \
    "thread local is not supported on this compiler. Fallback to global (non-thread-safe) storage."
#define EXT_TLS
#endif
#else
#define EXT_TLS
#endif  // EXTLIB_THREADSAFE

// -----------------------------------------------------------------------------
// SECTION: Macros
//

// assert and unreachable macro with custom message.
// Assert is disabled when compiling with NDEBUG, unreachable is instead replaced with compiler
// intrinsics on gcc, clang and msvc.
#ifndef NDEBUG
#ifndef EXTLIB_NO_STD
#define EXT_ASSERT(cond, msg)                                                                    \
    ((cond) ? ((void)0)                                                                          \
            : (fprintf(stderr, "%s:%d: error: %s failed: %s\n", __FILE__, __LINE__, #cond, msg), \
               abort()))

#define EXT_UNREACHABLE() \
    (fprintf(stderr, "%s:%d: error: reached unreachable code\n", __FILE__, __LINE__), abort())
#else
#define EXT_ASSERT(cond, msg) assert((cond) && msg)
#define EXT_UNREACHABLE()     assert(false && "reached unreachable code")
#endif  // EXTLIB_NO_STD

#else
#define EXT_ASSERT(cond, msg) ((void)(cond))

#if defined(__GNUC__) || defined(__clang__)
#define EXT_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#include <stdlib.h>
#define EXT_UNREACHABLE() __assume(0)
#else
#define EXT_UNREACHABLE()
#endif  // defined(__GNUC__) || defined(__clang__)

#endif  // NDEBUG

// TODO macro: crashes the program upon execution
#ifndef EXTLIB_NO_STD
#define EXT_TODO(name) (fprintf(stderr, "%s:%d: todo: %s\n", __FILE__, __LINE__, name), abort())
#endif  // EXTLIB_NO_STD

#define EXT_CONCAT2_(pre, post) pre##post
#define EXT_CONCAT_(pre, post)  EXT_CONCAT2_(pre, post)

// Portable static assertion: asserts a value at compile time
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(EXTLIB_NO_STD)
#include <assert.h>
#define EXT_STATIC_ASSERT static_assert
#else
#define EXT_STATIC_ASSERT(cond, msg)            \
    typedef struct {                            \
        int static_assertion_failed : !!(cond); \
    } EXT_CONCAT_(EXT_CONCAT_(static_assertion_failed_, __COUNTER__), __LINE__)
#endif  // defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(EXTLIB_NO_STD)

// Debug macro: prints an expression to stderr and returns its value.
//
// USAGE
// ```c
// if (DBG(x.size > 0)) {
//     ...
// }
// ```
// This will print to stderr:
// ```sh
// <file>:<line>: x.size > 0 = <result>
// ```
// where result is the value of the expression (0 or 1 in this case)
//
// NOTE
// This is available only when compiling in c11 mode, or when using clang or gcc.
// Not available when compiling without stdlib support.
#if ((defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || defined(__GNUC__)) && \
    !defined(EXTLIB_NO_STD)
#define EXT_DBG(x)                                                   \
    _Generic((x),                                                    \
        char: ext_dbg_char,                                          \
        signed char: ext_dbg_signed_char,                            \
        unsigned char: ext_dbg_unsigned_char,                        \
        short: ext_dbg_short,                                        \
        unsigned short: ext_dbg_unsigned_short,                      \
        int: ext_dbg_int,                                            \
        unsigned int: ext_dbg_unsigned_int,                          \
        long: ext_dbg_long,                                          \
        unsigned long: ext_dbg_unsigned_long,                        \
        long long: ext_dbg_long_long,                                \
        unsigned long long: ext_dbg_unsigned_long_long,              \
        float: ext_dbg_float,                                        \
        double: ext_dbg_double,                                      \
        long double: ext_dbg_long_double,                            \
        char *: ext_dbg_str,                                         \
        void *: ext_dbg_voidptr,                                     \
        const void *: ext_dbg_cvoidptr,                              \
        const char *: ext_dbg_cstr,                                  \
        const signed char *: ext_dbg_cptr_signed_char,               \
        signed char *: ext_dbg_ptr_signed_char,                      \
        const unsigned char *: ext_dbg_cptr_unsigned_char,           \
        unsigned char *: ext_dbg_ptr_unsigned_char,                  \
        const short *: ext_dbg_cptr_short,                           \
        short *: ext_dbg_ptr_short,                                  \
        const unsigned short *: ext_dbg_cptr_unsigned_short,         \
        unsigned short *: ext_dbg_ptr_unsigned_short,                \
        const int *: ext_dbg_cptr_int,                               \
        int *: ext_dbg_ptr_int,                                      \
        const unsigned int *: ext_dbg_cptr_unsigned_int,             \
        unsigned int *: ext_dbg_ptr_unsigned_int,                    \
        const long *: ext_dbg_cptr_long,                             \
        long *: ext_dbg_ptr_long,                                    \
        const unsigned long *: ext_dbg_cptr_unsigned_long,           \
        unsigned long *: ext_dbg_ptr_unsigned_long,                  \
        const long long *: ext_dbg_cptr_long_long,                   \
        long long *: ext_dbg_ptr_long_long,                          \
        const unsigned long long *: ext_dbg_cptr_unsigned_long_long, \
        unsigned long long *: ext_dbg_ptr_unsigned_long_long,        \
        const float *: ext_dbg_cptr_float,                           \
        float *: ext_dbg_ptr_float,                                  \
        const double *: ext_dbg_cptr_double,                         \
        double *: ext_dbg_ptr_double,                                \
        const long double *: ext_dbg_cptr_long_double,               \
        long double *: ext_dbg_ptr_long_double,                      \
        Ext_StringSlice: ext_dbg_ss,                                 \
        Ext_StringBuffer: ext_dbg_sb,                                \
        Ext_StringBuffer *: ext_dbg_ptr_sb,                          \
        default: ext_dbg_unknown)(#x, __FILE__, __LINE__, (x))
#endif  // ((defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || defined(__GNUC__))
        // && !defined(EXTLIB_NO_STD)

// Returns the required padding to align `o` to `s` bytes. `s` must be a power of two.
#define EXT_ALIGN_PAD(o, s) (-(uintptr_t)(o) & (s - 1))
// Returns `o` rounded up to the next multiple of `s`. `s` must be a power of two.
#define EXT_ALIGN_UP(o, s) (((uintptr_t)(o) + (s) - 1) & ~((uintptr_t)(s) - 1))

// Specifies minimum alignment for a variable declaration (C99-compatible)
#if defined(_MSC_VER)
#define EXT_ALIGNAS(n) __declspec(align(n))
#elif defined(__GNUC__) || defined(__clang__)
#define EXT_ALIGNAS(n) __attribute__((aligned(n)))
#else
#define EXT_ALIGNAS(n)
#endif

// Returns the number of elements of a c array
#define EXT_ARR_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define EXT_KiB(n) ((size_t)(n) << 10)
#define EXT_MiB(n) ((size_t)(n) << 20)
#define EXT_GiB(n) ((size_t)(n) << 30)

// Make the compiler check for correct arguments in format string
#if defined(__MINGW32__) || defined(__MINGW64__)
#define EXT_PRINTF_FORMAT(a, b) __attribute__((__format__(__MINGW_PRINTF_FORMAT, a, b)))
#elif __GNUC__
#define EXT_PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))
#else
#define EXT_PRINTF_FORMAT(a, b)
#endif  // __GNUC__

// Executes `start` expression at the start of the scope, and `end` expressions when it ends.
//
// It is implemented by exploiting how a `for` loop has an inizialization and an increment
// expression that run at the start and at the end of the loop, respectiverly.
// NOTE: care must be taken when returning early or breaking inside of the loop, because in that
// case the `end` expression will *not* be executed
//
// USAGE:
// ```c
// void* mem = ext_alloc(...);
// defer_loop((void)0, ext_free(mem)) {
//     // use mem, it will be freed at the end of the scope
// }
// ```
#define ext_defer_loop(begin, end) for(int i__ = ((begin), 0); i__ != 1; i__ = ((end), 1))
#define EXT_DEFER_LOOP             ext_defer_loop

// Assigns passed in value to variable, and jumps to label.
//
// USAGE:
// ```c
// bool res = true;
// // ... do some fallible operations
// if(err) return_exit(false);           // This will assign false to `res` and jumps to exit
// if(err) return_exit(false, exit)      // Can also customize the label (second arg)
// if(err) return_exit(false, exit, res) // Can also customize the variable that will be assigned
//                                       // (third arg)
//
// exit:
//     // ... free stuff
//     return res;
// ```
#define ext_return_exit(...) \
    ext__return_tox_(__VA_ARGS__, ext__return_to3_, ext__return_to2_, ext__return_to1_)(__VA_ARGS__)

// -----------------------------------------------------------------------------
// SECTION: Logging
//

typedef enum {
    EXT_DEBUG,
    EXT_INFO,
    EXT_WARNING,
    EXT_ERROR,
    EXT_NO_LOGGING,
} Ext_LogLevel;

// Custom logging function used in the context to configure logger
typedef void (*Ext_LogFn)(Ext_LogLevel lvl, void *data, const char *fmt, va_list ap);
// Log a message with severity level `lvl`.
void ext_log(Ext_LogLevel lvl, const char *fmt, ...) EXT_PRINTF_FORMAT(2, 3);
void ext_logvf(Ext_LogLevel lvl, const char *fmt, va_list ap);

// -----------------------------------------------------------------------------
// SECTION: Context
//

// The context is a global state variable that defines the current behaviour for allocations and
// logging of the program.
// A new context can be pushed at any time with a custom `Allocator` and custom functions for
// logging, making all code between a push/pop pair use these new behaviours.
// The default context created at the start of a program is configured with a default allocator that
// uses malloc/realloc/free, and a default logging function that prints to stdout/stderr
//
// USAGE
// ```c
// Allocator a = ...;               // your custom allocator
// Context new_ctx = *ext_context;  // Copy the current context
// new_ctx.alloc = &a;              // configure the new allocator
// new_ctx.log_fn = custom_log;     // configure the new logger
// push_context(&new_ctx);          // make the context the current one
//      // Code in here will use new behaviours
// pop_context();                   // Remove context, restores previous one
// ```
//
// NOTE
// The context stack is implemented as a linked list in `static` memory. To make the program
// threadsafe, compile with EXTLIB_THREADSAFE flag, that will use thread local storage to provide
// a local context stack for each thread
typedef struct Ext_Context {
    struct Ext_Context *prev;
    struct Ext_Allocator *alloc;
    Ext_LogLevel log_level;
    void *log_data;
    Ext_LogFn log_fn;
} Ext_Context;

// The current context
extern EXT_TLS Ext_Context *ext_context;

// Pushes a new context to the stack, making it the current one
void ext_push_context(Ext_Context *ctx);
// Pops a context from the stack, restoring the previous one
Ext_Context *ext_pop_context(void);

// Utility macro to push/pop context between `code`
//
// USAGE:
// ```c
// PUSH_CONTEXT(&myctx) {
//    // ... do stuff
// }
// // ... context automatically popped
// ```
#define EXT_PUSH_CONTEXT(ctx) EXT_DEFER_LOOP(ext_push_context(ctx), ext_pop_context())

// Utility macro to push/pop context with an allocator between code.
// Simplifies pushing when the only thing you want to customize is the allocator.
//
// USAGE:
// ```c
// PUSH_ALLOCATOR(&temp_allocator.base) {
//    // ... do stuff
// }
// // ... context automatically popped
// ```
#define EXT_PUSH_ALLOCATOR(allocator)                       \
    Ext_Context EXT_CONCAT_(ctx_, __LINE__) = *ext_context; \
    EXT_CONCAT_(ctx_, __LINE__).alloc = (allocator);        \
    EXT_DEFER_LOOP(ext_push_context(&EXT_CONCAT_(ctx_, __LINE__)), ext_pop_context())

// Utility macro to push/pop a context with the given logging level set.
// Simplifies pushing when the only thing you want to customize is the logging level.
//
// USAGE:
// ```c
// LOGGING_LEVEL(EXT_NO_LOGGING) {
//    // ... do stuff
//    // ... nothing will log anymore between these
// }
// // ... context automatically popped
// ```
#define EXT_LOGGING_LEVEL(level)                            \
    Ext_Context EXT_CONCAT_(ctx_, __LINE__) = *ext_context; \
    EXT_CONCAT_(ctx_, __LINE__).log_level = (level);        \
    EXT_DEFER_LOOP(ext_push_context(&EXT_CONCAT_(ctx_, __LINE__)), ext_pop_context())

// -----------------------------------------------------------------------------
// SECTION: Allocators
//
// Custom allocator and temporary allocator

// Allocator defines a set of configurable functions for dynamic memory allocation.
// A csutom allocator is a struct that defines `Allocator` as its first field, usually called
// `base`, so that it can be freely cast to `Allocator*` either via direct casting: `(Allocator*)
// my_custom_allocator` or by taking a pointer to its base field: `&my_custom_allocator.base`.
//
// USAGE
// ```c
// typedef struct {
//     Ext_Allocator base;
//     // other fields you need for your allocator
// } MyNewAllocator;
//
// void* my_allocator_alloc_fn(Ext_Allocator* a, size_t size) {
//     MyNewAllocator *my_alloc = (MyNewAllocator*)a;
//     void* ptr;
//     // ... do what you need to allocate
//     return ptr;
// }
// // ... other allocator functions (my_allocator_realloc_fn, my_allocator_free_fn)
//
// MyNewAllocator my_new_allocator = {
//     {my_allocator_alloc_fn, my_allocator_realloc_fn, my_allocator_free_fn},
//     // other fields of your allocator
// }
// ```
//
// Then, you can use your new allocator directly, or configure it as the current one by pushing it
// in a context:
//
// ```c
// Context new_ctx = *ext_context;
// new_ctx.alloc = &my_new_allocator.base;
// push_context(&new_ctx);
//     // ...
// pop_context()
// ```
typedef struct Ext_Allocator {
    void *(*alloc)(struct Ext_Allocator *, size_t size);
    void *(*realloc)(struct Ext_Allocator *, void *ptr, size_t old_size, size_t new_size);
    void (*free)(struct Ext_Allocator *, void *ptr, size_t size);
} Ext_Allocator;

// Macros to simplify memory allocation. They use the current configured context allocator
// ext_new:
//   Allocates a new value of size `sizeof(T)` using `ext_alloc`
// ext_new_array:
//   Allocates a new array of size `sizeof(T)*count` using `ext_alloc`
// ext_delete:
//   Deletes allocated memory of `sizeof(T)` using `ext_free`
// ext_delete_array:
//   Deletes an array of `sizeof(T)*count` using `ext_free`
// ext_clone:
//   Creates a copy of the provided pointer using `ext_memdup`
#define ext_new(T)                      ext_alloc(sizeof(T))
#define ext_new_array(T, count)         ext_alloc(sizeof(T) * count)
#define ext_delete(T, ptr)              ext_free(ptr, sizeof(T))
#define ext_delete_array(T, count, ptr) ext_free(ptr, sizeof(T) * count);
#define ext_clone(T, ptr)               ext_memdup(ptr, sizeof(T));
// Similar to above, but work with any Ext_Allocator
#define ext_allocator_new(a, T)                      ext_allocator_alloc(a, sizeof(T))
#define ext_allocator_new_array(a, T, count)         ext_allocator_alloc(a, sizeof(T) * count)
#define ext_allocator_delete(a, T, ptr)              ext_allocator_free(a, ptr, sizeof(T))
#define ext_allocator_delete_array(a, T, count, ptr) ext_allocator_free(a, ptr, sizeof(T) * count)
#define ext_allocator_clone(a, T, ptr)               ext_allocator_memdup(a, ptr, sizeof(T))

// Generic allocator functions - work with any Ext_Allocator
inline void *ext_allocator_alloc(Ext_Allocator *a, size_t size) {
    return a->alloc(a, size);
}
inline void *ext_allocator_realloc(Ext_Allocator *a, void *ptr, size_t old_sz, size_t new_sz) {
    return a->realloc(a, ptr, old_sz, new_sz);
}
inline void ext_allocator_free(Ext_Allocator *a, void *ptr, size_t size) {
    a->free(a, ptr, size);
}
char *ext_allocator_strdup(Ext_Allocator *a, const char *s);
void *ext_allocator_memdup(Ext_Allocator *a, const void *mem, size_t size);
// Backward compatibility: old _alloc suffix functions now use ext_allocator_* internally
// Note: parameter order changed - allocator is now first parameter
// DEPRECATED: Use ext_allocator_strdup and ext_allocator_memdup instead
#define ext_strdup_alloc(s, a)         ext_allocator_strdup(a, s)
#define ext_memdup_alloc(mem, size, a) ext_allocator_memdup(a, mem, size)

// Allocation functions that use the current configured context to allocate, reallocate and free
// memory.
// It is reccomended to always use these functions instead of malloc/realloc/free when you need
// memory to make the behaviour of your code configurable via the context.
inline void *ext_alloc(size_t size) {
    return ext_allocator_alloc(ext_context->alloc, size);
}
inline void *ext_realloc(void *ptr, size_t old_sz, size_t new_sz) {
    return ext_allocator_realloc(ext_context->alloc, ptr, old_sz, new_sz);
}
inline void ext_free(void *ptr, size_t size) {
    ext_allocator_free(ext_context->alloc, ptr, size);
}
// Copies a cstring by using the current context allocator
inline char *ext_strdup(const char *s) {
    return ext_allocator_strdup(ext_context->alloc, s);
}
// Copies a memory region of `size` bytes by using the current context allocator
inline void *ext_memdup(const void *mem, size_t size) {
    return ext_allocator_memdup(ext_context->alloc, mem, size);
}

// A default allocator that uses malloc/realloc/free.
// It is the allocator configured in the context at program start.
typedef struct Ext_DefaultAllocator {
    Ext_Allocator base;
} Ext_DefaultAllocator;
extern Ext_DefaultAllocator ext_default_allocator;

// -----------------------------------------------------------------------------
// SECTION: Temporary allocator
//

// The temporary allocator supports creating temporary dynamic allocations, usually short lived.
// By default, it uses a predefined amount of `static` memory to allocate (see
// `EXT_DEFAULT_TEMP_SIZE`) and never frees memory. You should instead either `temp_reset` or
// `temp_rewind` at appropriate points of your program to avoid running out of temp memory.
// If the temp allocator runs out of memory, it will `abort` the program with an error message.
//
// NOTE
// If you want to use the temp allocator from other threads, ensure to configure a new memory region
// with `temp_set_mem` for each thread, as by default all temp allocators point to the same shared
// `static` memory area.
typedef struct Ext_TempAllocator {
    Ext_Allocator base;
    char *start, *end;
    size_t mem_size;
    void *mem;
} Ext_TempAllocator;
// The global temp allocator
extern EXT_TLS Ext_TempAllocator ext_temp_allocator;

// Sets a new memory area for temporary allocations
void ext_temp_set_mem(void *mem, size_t size);
// Allocates `size` bytes of memory from the temporary area
void *ext_temp_alloc(size_t size);
// Reallocates `new_size` bytes of memory from the temporary area.
// If `*ptr` is the result of the last allocation, it resizes the allocation in-place.
// Otherwise, it simly creates a new allocation of `new_size` and copies over the content.
void *ext_temp_realloc(void *ptr, size_t old_size, size_t new_size);
// How much temp memory is available
size_t ext_temp_available(void);
// Resets the whole temporary allocator. You should prefer using `temp_checkpoint` and `temp_rewind`
// instead of this function, so that allocations before the checkpoint remain valid.
void ext_temp_reset(void);
// `temp_checkpoint` checkpoints the current state of the temporary allocator, and `temp_rewind`
// rewinds the state to the saved point.
//
// USAGE
// ```c
// int process(void) {
//     void* checkpoint = temp_checkpoint();
//     for(int i = 0; i < 1000; i++) {
//         // temp allocate at your heart's content
//     }
//
//     // ... do something with all your temp memory
//
//     temp_rewind(checkpoint);  // free all temp data at once, allocations before the checkpoint
//                               // remain valid
//
//     return result;
// }
// ```
void *ext_temp_checkpoint(void);
void ext_temp_rewind(void *checkpoint);
// Copies a cstring into temp memory
char *ext_temp_strdup(const char *str);
// Copies a memory region of `size` bytes into temp memory
void *ext_temp_memdup(void *mem, size_t size);
#ifndef EXTLIB_NO_STD
// Allocate and format a string into temp memory
char *ext_temp_sprintf(const char *fmt, ...) EXT_PRINTF_FORMAT(1, 2);
char *ext_temp_vsprintf(const char *fmt, va_list ap);
#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: Arena allocator
//

// An allocated chunk in the arena
typedef struct Ext_ArenaPage {
    struct Ext_ArenaPage *next;
    size_t size;
    size_t base;
    size_t pos;
    char data[];
} Ext_ArenaPage;

// Saved Arena state at a point in time
typedef struct {
    Ext_ArenaPage *page;
    size_t pos;
} Ext_ArenaCheckpoint;

typedef enum {
    EXT_ARENA_NONE = 0,
    // Zeroes the memory allocated by the arena.
    EXT_ARENA_ZERO_ALLOC = 1 << 0,
    // The arena will not allocate pages larger than the configured page size, even if the user
    // requests an allocation that is larger than a single page.
    // If it can't allocate, aborts with an error.
    EXT_ARENA_FIXED_PAGE_SIZE = 1 << 1,
    // Do not chain page chunks.
    // If allocations exceed a single page worth of memory, aborts with an error.
    EXT_ARENA_NO_CHAIN = 1 << 2,
} Ext_ArenaFlags;

// Arena implements an arena allocator that allocates memory chunks inside larger pre-allocated
// pages.
// An arena allocator simplifies memory management by allowing multiple allocations to be freed
// as a group, as well as enabling efficient memory management by reusing a previously reset arena.
// `Arena` conforms to the `Allocator` interface, making it possible to use it as a context
// allocator, or the allocator of a dynamic array or hashmap.
//
// USAGE
// ```c
// Arena a = make_arena()           // creates an arena with default parameters
// a = make_arena(.alignment = 32) // or, initialize the arena with custom parameters
//
// // ... allocate memory, push it as context allocator, etc...
//
// arena_reset(&a)              // reset the arena all at once
// arena_rewind(&a, checkpoint) // or, better yet, restore its state to a checkpoint, so that
//                              // allocations before it remain valid (see `arena_checkpoint()`)
//
// arena_destroy(&a) // destroy the arena, freeing all memory associated with it
// ```
typedef struct Ext_Arena {
    Ext_Allocator base;
    // `Allocator` used to allocate pages. By default uses the current context allocator.
    Ext_Allocator *page_allocator;
    // The alignment of the allocations returned by the arena. By default is
    // `EXT_DEFAULT_ALIGNMENT`.
    size_t alignment;
    // The page size of the arena. By default it's `EXT_ARENA_PAGE_SZ`.
    size_t page_size;
    // Arena flags. See `ArenaFlags` enum
    Ext_ArenaFlags flags;
    // Private fields
    Ext_ArenaPage *first_page, *last_page;
} Ext_Arena;

// Creates a new arena. Defined as a macro so it can be used in a const context.
//
// USAGE
// ```c
// // Customize parameters
// Arena a = make_arena(.page_allocator = &my_allocator, .alignment = 8)
// // Default parameters
// Arena def_arena = make_arena();
// ```
// See `Ext_Arena` struct for all available options
#define ext_make_arena(...)                                                 \
    ((Ext_Arena){.base = {ext__arena_alloc_wrap_, ext__arena_realloc_wrap_, \
                          ext__arena_free_wrap_},                           \
                 .alignment = EXT_DEFAULT_ALIGNMENT,                        \
                 .page_size = EXT_ARENA_PAGE_SZ,                            \
                 __VA_ARGS__})

#define ext_arena_push(a, T)              ext_arena_alloc(a, sizeof(T))
#define ext_arena_push_array(a, T, n)     ext_arena_alloc(a, sizeof(T) * (n))
#define ext_arena_pop(a, T, ptr)          ext_arena_free(a, ptr, sizeof(T))
#define ext_arena_pop_array(a, T, n, ptr) ext_arena_free(a, ptr, sizeof(T) * (n))

// Allocates `size` bytes in the arena
void *ext_arena_alloc(Ext_Arena *a, size_t size);
// Reallocates `new_size` bytes. If `ptr` is the pointer of the last allocation, it tries to grow
// the allocation in-place. Otherwise, it allocates a new region of `new_size` bytes and copies the
// data over.
void *ext_arena_realloc(Ext_Arena *a, void *ptr, size_t old_size, size_t new_size);
// Frees a previous allocation of `size` bytes. It only actually frees data if `ptr` is the pointer
// of the last allocation, as only the last one can be freed in-place.
void ext_arena_free(Ext_Arena *a, void *ptr, size_t size);
// `arena_checkpoint` checkpoints the current state of the arena, and `arena_rewind` rewinds the
// state to the saved point.
//
// USAGE
// ```c
// int process(Arena* a) {
//     ArenaCheckpoint checkpoint = arena_checkpoint(a);
//     for(int i = 0; i < 1000; i++) {
//         // allocate at your heart's content
//     }
//
//     // ... do something with all the memory
//
//     arena_rewind(a, checkpoint); // Free all allocated memory up to the checkpoint. Previous
//                                  // allocations remain valid
//
//     return result;
// }
// ```
Ext_ArenaCheckpoint ext_arena_checkpoint(const Ext_Arena *a);
void ext_arena_rewind(Ext_Arena *a, Ext_ArenaCheckpoint checkpoint);
// Resets the whole arena.
void ext_arena_reset(Ext_Arena *a);
// Frees all memory allocated in the arena and resets it.
void ext_arena_destroy(Ext_Arena *a);
// Gets the currently allocated bytes in the arena
size_t ext_arena_get_allocated(const Ext_Arena *a);
// Copies a cstring by allocating it in the arena
char *ext_arena_strdup(Ext_Arena *a, const char *str);
// Copies a memory region of `size` bytes by allocating it in the arena
void *ext_arena_memdup(Ext_Arena *a, const void *mem, size_t size);
#ifndef EXTLIB_NO_STD
// Allocate and format a string into the arena
char *ext_arena_sprintf(Ext_Arena *a, const char *fmt, ...) EXT_PRINTF_FORMAT(2, 3);
char *ext_arena_vsprintf(Ext_Arena *a, const char *fmt, va_list ap);
#endif

// -----------------------------------------------------------------------------
// SECTION: Dynamic array
//
// A growable, type-safe, dynamic array implemented as macros.
// The dynamic array integrates with the `Allocator` interface and the context to support custom
// allocators for its backing array.
//
// USAGE;
//```c
// typedef struct {
//     int* items;
//     size_t capacity, size;
//     Allocator* allocator;
// } IntArray;
//
// IntArray a = {0};
// // By default, the array will use the current context allocator on the first allocation
// array_push(&a, 1);
// array_push(&a, 2);
// // Frees memory via the array's allocator
// array_free(&a);
//
// // Explicitely use custom allocator
// IntArray a2 = {0};
// a2.allocator = &ext_temp_allocator.base;
// // push, remove insert etc...
// temp_reset(); // Reset all at once
//```

// Inital size of the backing array on first allocation
#ifndef EXT_ARRAY_INIT_CAP
#define EXT_ARRAY_INIT_CAP 8
#endif  // EXT_ARRAY_INIT_CAP

// Macro to iterate over all elements
//
// USAGE
// ```c
// IntArray a = {0};
// // push elems...
// array_foreach(int, it, &a) {
//     printf("%d\n", *it);
// }
// ```
#define ext_array_foreach(T, it, vec)                                      \
    for(T *it = (vec)->items, *end = (void *)((vec)->items + (vec)->size); \
        (void *)it < (void *)end; it++)

// Reserves at least `requested_cap` elements in the dynamic array, growing the backing array if
// necessary. `requested_cap` is treated as an absolute value, so if you want to take the current
// size into account you'll have to do it yourself: `array_reserve(&a, a.size + new_cap)`.
#define ext_array_reserve(arr, requested_cap)                                           \
    do {                                                                                \
        if((arr)->capacity < (requested_cap)) {                                         \
            size_t oldcap = (arr)->capacity;                                            \
            size_t newcap = (arr)->capacity ? (arr)->capacity * 2 : EXT_ARRAY_INIT_CAP; \
            while(newcap < (requested_cap)) newcap *= 2;                                \
            if(!((arr)->allocator)) (arr)->allocator = (void *)ext_context->alloc;      \
            Ext_Allocator *a = (Ext_Allocator *)(arr)->allocator;                       \
            if(!(arr)->items) {                                                         \
                (arr)->items = ext_allocator_alloc(a, newcap * sizeof(*(arr)->items));  \
            } else {                                                                    \
                (arr)->items = ext_allocator_realloc(a, (arr)->items,                   \
                                                     oldcap * sizeof(*(arr)->items),    \
                                                     newcap * sizeof(*(arr)->items));   \
            }                                                                           \
            (arr)->capacity = newcap;                                                   \
        }                                                                               \
    } while(0)

// Reserves at exactly `requested_cap` elements in the dynamic array, growing the backing array
// if necessary. `requested_cap` is treated as an absolute value.
#define ext_array_reserve_exact(arr, requested_cap)                                    \
    do {                                                                               \
        if((arr)->capacity < (requested_cap)) {                                        \
            size_t oldcap = (arr)->capacity;                                           \
            size_t newcap = (requested_cap);                                           \
            if(!((arr)->allocator)) (arr)->allocator = (void *)ext_context->alloc;     \
            Ext_Allocator *a = (Ext_Allocator *)(arr)->allocator;                      \
            if(!(arr)->items) {                                                        \
                (arr)->items = ext_allocator_alloc(a, newcap * sizeof(*(arr)->items)); \
            } else {                                                                   \
                (arr)->items = ext_allocator_realloc(a, (arr)->items,                  \
                                                     oldcap * sizeof(*(arr)->items),   \
                                                     newcap * sizeof(*(arr)->items));  \
            }                                                                          \
            (arr)->capacity = newcap;                                                  \
        }                                                                              \
    } while(0)

// Appends a new element in the array, growing if necessary
#define ext_array_push(a, v)                   \
    do {                                       \
        ext_array_reserve((a), (a)->size + 1); \
        (a)->items[(a)->size++] = (v);         \
    } while(0)

// Frees the dynamic array
#define ext_array_free(a)                                                   \
    do {                                                                    \
        if((a)->allocator) {                                                \
            ext_allocator_free((Ext_Allocator *)(a)->allocator, (a)->items, \
                               (a)->capacity * sizeof(*(a)->items));        \
        }                                                                   \
        memset((a), 0, sizeof(*(a)));                                       \
    } while(0)

// Appends all `count` elements into the dynamic array
#define ext_array_push_all(a, elems, count)                                     \
    do {                                                                        \
        ext_array_reserve(a, (a)->size + (count));                              \
        memcpy((a)->items + (a)->size, (elems), (count) * sizeof(*(a)->items)); \
        (a)->size += (count);                                                   \
    } while(0)

// Removes and returns the last element in the dynamic array. Complexity O(1).
#define ext_array_pop(a) (EXT_ASSERT((a)->size > 0, "no items to pop"), (a)->items[--(a)->size])

// Removes the element at `idx`. Shifts all other elements to the left to compact the array, so
// it has complexity O(n).
#define ext_array_remove(a, idx)                                            \
    do {                                                                    \
        EXT_ASSERT((size_t)(idx) < (a)->size, "array index out of bounds"); \
        if((size_t)(idx) < (a)->size - 1) {                                 \
            memmove((a)->items + (idx), (a)->items + (idx) + 1,             \
                    ((a)->size - idx - 1) * sizeof(*(a)->items));           \
        }                                                                   \
        (a)->size--;                                                        \
    } while(0)

// Removes the element at `idx` by swapping it with the last element of the array. It doesn't
// preseve order, but has O(1) complexity.
#define ext_array_swap_remove(a, idx)                                       \
    do {                                                                    \
        EXT_ASSERT((size_t)(idx) < (a)->size, "array index out of bounds"); \
        if((size_t)(idx) < (a)->size - 1) {                                 \
            (a)->items[idx] = (a)->items[(a)->size - 1];                    \
        }                                                                   \
        (a)->size--;                                                        \
    } while(0)

// Removes all elements from the array. Complexity O(1).
#define ext_array_clear(a) \
    do {                   \
        (a)->size = 0;     \
    } while(0)

// Resizes the array in place so that its size is `new_sz`. If `new_sz` is greater than the
// current size, the array is extended by the difference, otherwise it is simply truncated.
// Beware that in case `new_size > size` the new elements will be uninitialized.
#define ext_array_resize(a, new_sz)             \
    do {                                        \
        ext_array_reserve_exact((a), (new_sz)); \
        (a)->size = new_sz;                     \
    } while(0)

// Shrinks the capacity of the array to fit its size. The resulting backing array will be
// exactly `size * sizeof(*array.items)` bytes.
#define ext_array_shrink_to_fit(a)                                                        \
    do {                                                                                  \
        if((a)->capacity > (a)->size) {                                                   \
            if((a)->size == 0) {                                                          \
                (a)->allocator->free((a)->allocator, (a)->items,                          \
                                     (a)->capacity * sizeof(*(a)->items));                \
                (a)->items = NULL;                                                        \
            } else {                                                                      \
                (a)->items = (a)->allocator->realloc((a)->allocator, (a)->items,          \
                                                     (a)->capacity * sizeof(*(a)->items), \
                                                     (a)->size * sizeof(*(a)->items));    \
            }                                                                             \
            (a)->capacity = (a)->size;                                                    \
        }                                                                                 \
    } while(0);

// -----------------------------------------------------------------------------
// SECTION: String buffer
//

// A dynamic and growable `char*` buffer that can be used for assembling strings or generic byte
// buffers.
// Internally is implemented as a dynamic array of `char`s
// BEWARE: the buffer is not NUL terminated, so if you want to use it as a cstring you'll need to
// `sb_append_char(&sb, '\0')`
//
// USAGE
//```c
// StringBuffer sb = {0};
// // By default, the buffer will use the current context allocator on the first allocation
// sb_appendf("%s:%d: look ma' a formatted string!", __FILE__, __LINE__);
// sb_append_char(&sb, '\0');
// printf("%s\n", sb.items);
// sb_free(&sb);
//
// // Explicitely use custom allocator
// StringBuffer sb2 = {0};
// sb2.allocator = &ext_temp_allocator.base;
// // append `char`s...
// temp_reset(); // Reset all at once
//```
typedef struct {
    char *items;
    size_t capacity, size;
    Ext_Allocator *allocator;
} Ext_StringBuffer;

// Format specifier for a string buffer. Allows printing a non-NUL terminalted buffer
//
// USAGE
// ```c
// printf("Bufer: "SB_Fmt"\n", SB_Arg(sb));
// ```
#define Ext_SB_Fmt     "%.*s"
#define Ext_SB_Arg(ss) (int)(ss).size, (ss).items

// Frees the string buffer
#define ext_sb_free(sb) ext_array_free(sb)
// Appends a single char to the string buffer
#define ext_sb_append_char(sb, c) ext_array_push(sb, c)
// Appends a memory region of `size` bytes to the buffer
#define ext_sb_append(sb, mem, size) ext_array_push_all(sb, mem, size)
// Appends a cstring to the buffer
#define ext_sb_append_cstr(sb, str)       \
    do {                                  \
        const char *s_ = (str);           \
        size_t len_ = strlen(s_);         \
        ext_array_push_all(sb, s_, len_); \
    } while(0)

// Prepends a memory region of `size` bytes to the buffer. shifts all elements right. Complexity
// O(n).
#define ext_sb_prepend(sb, mem, count)                         \
    do {                                                       \
        ext_array_reserve(sb, (sb)->size + (count));           \
        memmove((sb)->items + count, (sb)->items, (sb)->size); \
        memcpy((sb)->items, mem, count);                       \
        (sb)->size += count;                                   \
    } while(0)

// Prepends a cstring to the buffer. shifts all elements right. Complexity O(n).
#define ext_sb_prepend_cstr(sb, str)  \
    do {                              \
        const char *s_ = (str);       \
        size_t len_ = strlen(str);    \
        ext_sb_prepend(sb, s_, len_); \
    } while(0)

// Prepends a single char to the buffer. shifts all elements right. Complexity O(n).
#define ext_sb_prepend_char(sb, c)  \
    do {                            \
        char c_ = (c);              \
        ext_sb_prepend(sb, &c_, 1); \
    } while(0)

#define ext_sb_reserve(sb, requested_cap)       ext_array_reserve(sb, requested_cap)
#define ext_sb_reserve_exact(sb, requested_cap) ext_array_reserve_exact(sb, requested_cap)

// Replaces all characters appearing in `to_replace` with `replacement`.
void ext_sb_replace(Ext_StringBuffer *sb, size_t start, const char *to_replace, char replacement);
// Converts all characters in the buffer to uppercase in-place
void ext_sb_to_upper(Ext_StringBuffer *sb);
// Converts all characters in the buffer to lowercase in-place
void ext_sb_to_lower(Ext_StringBuffer *sb);
// Reverses the string buffer in-place
void ext_sb_reverse(Ext_StringBuffer *sb);
// Transforms the string buffer to a cstring, by appending NUL and shrinking it to fit its size.
// The string buffer is reset after this operation.
// BEWARE: you still need to free the returned string with the stringbuffer's allocator after this
// operation, otherwise memory will be leaked
char *ext_sb_to_cstr(Ext_StringBuffer *sb);
#ifndef EXTLIB_NO_STD
// Appends a formatted string to the string buffer
int ext_sb_appendf(Ext_StringBuffer *sb, const char *fmt, ...) EXT_PRINTF_FORMAT(2, 3);
int ext_sb_appendvf(Ext_StringBuffer *sb, const char *fmt, va_list ap);
#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: String slice
//

// A string slice is an immutable 'fat pointer' to a region of memory `data` of `size` bytes.
// It is reccomended to treat a string slice as a value struct, i.e. pass it and return it by value
// unless you absolutely need to pass it as a pointer (for example, if you need to modify it).
typedef struct {
    size_t size;
    const char *data;
} Ext_StringSlice;

// Iterates all the splits on `delim`
//
// USAGE:
// ```
// ss_foreach_split(SS("Cantami, o Diva"), ',', word) {
//     ext_log(INFO, "Word: " SS_Fmt, SS_Arg(word));
// }
// ```
#define ext_ss_foreach_split(ss, delim, var) \
    for(StringSlice var, ss_iter_ = (ss);    \
        ss_iter_.size && (var = ss_split_once(&ss_iter_, (delim)), true);)

// Iterates, in reverse order, all the splits on `delim`
#define ext_ss_foreach_rsplit(ss, delim, var) \
    for(StringSlice var, ss_iter_ = (ss);     \
        ss_iter_.size && (var = ss_rsplit_once(&ss_iter_, (delim)), true);)

// Iterates all the split on `delim` cstring
#define ext_ss_foreach_split_cstr(ss, delim, var) \
    for(StringSlice var, ss_iter_ = (ss);         \
        ss_iter_.size && (var = ss_split_once_cstr(&ss_iter_, (delim)), true);)

// Iterates, in reverse order, all the split on `delim` cstring
#define ext_ss_foreach_rsplit_cstr(ss, delim, var) \
    for(StringSlice var, ss_iter_ = (ss);          \
        ss_iter_.size && (var = ss_rsplit_once_cstr(&ss_iter_, (delim)), true);)

// Format specifier for a string slice.
//
// USAGE
// ```c
// printf("String slice: "SS_Fmt"\n", SB_Arg(ss));
// ```
#define Ext_SS_Fmt     "%.*s"
#define Ext_SS_Arg(ss) (int)(ss).size, (ss).data

// Creates a StringSlice from a cstring.
// This is a utility macro equivalent to doing: `ss_from_cstr(cstr)`
#define Ext_SS(cstr) ext_ss_from_cstr(cstr)

// Creates a new string slice from a string buffer. The string slice will act as a 'view' into
// the buffer.
#define ext_sb_to_ss(sb) (ext_ss_from((sb).items, (sb).size))
// Creates a new string slice from a region of memory of `size` bytes
Ext_StringSlice ext_ss_from(const void *mem, size_t size);
// Creates a new string slice from a cstring
Ext_StringSlice ext_ss_from_cstr(const char *str);
// Splits the string slice once on `delim`. The input string slice will be modified to point to the
// character after `delim`. The split prefix will be returned as a new string slice.
//
// USAGE
// ```c
//  StringSlice ss = ss_from_cstr("Cantami, o Diva, del Pelide Achille");
//  while(ss.size) {
//      StringSlice word = ss_split_once(&ss, ' ');
//      printf("Word: "SS_Fmt"\n", SS_Arg(word));
//  }
// ```
Ext_StringSlice ext_ss_split_once(Ext_StringSlice *ss, char delim);
// Same as `split_once` but from the end of the slice
Ext_StringSlice ext_ss_rsplit_once(Ext_StringSlice *ss, char delim);
// Same as split_once, but splits on any character preset in `set`
Ext_StringSlice ext_ss_split_once_any(Ext_StringSlice *ss, const char *set);
// Same as rsplit_once, but splits on any character preset in `set`
Ext_StringSlice ext_ss_rsplit_once_any(Ext_StringSlice *ss, const char *set);
// Same as `split_once` but matches all whitespace characters as defined in libc's `isspace`.
Ext_StringSlice ext_ss_split_once_ws(Ext_StringSlice *ss);
// Same as `rsplit_once` but matches all whitespace characters as defined in libc's `isspace`.
Ext_StringSlice ext_ss_rsplit_once_ws(Ext_StringSlice *ss);
// Same as `split_once` but on a multi character delimiter
Ext_StringSlice ext_ss_split_once_cstr(Ext_StringSlice *ss, const char *delim);
// Same as `rsplit_once` but on a multi character delimiter
Ext_StringSlice ext_ss_rsplit_once_cstr(Ext_StringSlice *ss, const char *delim);
// Finds the first occurence of `c` starting from `offset`, or -1 if not found
ptrdiff_t ext_ss_find_char(Ext_StringSlice ss, char c, size_t offset);
// Like `ss_find_char`, but finds the last occurence starting from offset.
ptrdiff_t ext_ss_rfind_char(Ext_StringSlice ss, char c, size_t offset);
// Like `ss_find_char`, but finds the first occurence of `needle` starting from `offset`.
ptrdiff_t ext_ss_find(Ext_StringSlice ss, Ext_StringSlice needle, size_t offset);
// Like `ss_rfind_char`, but finds the last occurence of `needle` starting from `offset`.
ptrdiff_t ext_ss_rfind(Ext_StringSlice ss, Ext_StringSlice needle, size_t offset);
// Like `ss_find`, but takes a C string needle.
ptrdiff_t ext_ss_find_cstr(Ext_StringSlice ss, const char *needle, size_t offset);
// Like `ss_rfind`, but takes a C string needle.
ptrdiff_t ext_ss_rfind_cstr(Ext_StringSlice ss, const char *needle, size_t offset);
// Returns a new string slice with all white space removed from the start
Ext_StringSlice ext_ss_trim_start(Ext_StringSlice ss);
// Returns a new string slice with all white space removed from the end
Ext_StringSlice ext_ss_trim_end(Ext_StringSlice ss);
// Returns a new string slice with all white space removed from both ends
Ext_StringSlice ext_ss_trim(Ext_StringSlice ss);
// Returns a new string slice starting from `n` bytes into `ss`.
Ext_StringSlice ext_ss_cut(Ext_StringSlice ss, size_t n);
// Returns a new string slice of size `n`.
Ext_StringSlice ext_ss_trunc(Ext_StringSlice ss, size_t n);
// Returns a substring starting at `start` of at most `len` bytes
Ext_StringSlice ext_ss_substr(Ext_StringSlice ss, size_t start, size_t len);
// Returns true if the given string slice starts with `prefix`
bool ext_ss_starts_with(Ext_StringSlice ss, Ext_StringSlice prefix);
// Returns true if the given string slice ends with `suffix`
bool ext_ss_ends_with(Ext_StringSlice ss, Ext_StringSlice suffix);
// Returns a new string slice with `prefix` removed, or the original slice if prefix is not present
Ext_StringSlice ext_ss_strip_prefix(Ext_StringSlice ss, Ext_StringSlice prefix);
// Returns a new string slice with `suffix` removed, or the original slice if suffix is not present
Ext_StringSlice ext_ss_strip_suffix(Ext_StringSlice ss, Ext_StringSlice suffix);
// Like `ss_strip_prefix`, but takes a C string prefix
Ext_StringSlice ext_ss_strip_prefix_cstr(Ext_StringSlice ss, const char *prefix);
// Like `ss_strip_suffix`, but takes a C string suffix
Ext_StringSlice ext_ss_strip_suffix_cstr(Ext_StringSlice ss, const char *suffix);
// memcompares two string slices
int ext_ss_cmp(Ext_StringSlice s1, Ext_StringSlice s2);
// Returns true if the two string slices are equal
bool ext_ss_eq(Ext_StringSlice s1, Ext_StringSlice s2);
// Returns true if the two string slices are equal, ignoring ASCII case
bool ext_ss_eq_ignore_case(Ext_StringSlice a, Ext_StringSlice b);
// Case-insensitive comparison (returns <0, 0, >0 like memcmp)
int ext_ss_cmp_ignore_case(Ext_StringSlice a, Ext_StringSlice b);
// Returns true if the given string slice starts with `prefix`, ignoring ASCII case
bool ext_ss_starts_with_ignore_case(Ext_StringSlice ss, Ext_StringSlice prefix);
// Returns true if the given string slice ends with `suffix`, ignoring ASCII case
bool ext_ss_ends_with_ignore_case(Ext_StringSlice ss, Ext_StringSlice suffix);
// Like `ss_starts_with_ignore_case`, but takes a C string prefix
bool ext_ss_starts_with_ignore_case_cstr(Ext_StringSlice ss, const char *prefix);
// Like `ss_ends_with_ignore_case`, but takes a C string suffix
bool ext_ss_ends_with_ignore_case_cstr(Ext_StringSlice ss, const char *suffix);
// Creates a cstring from the string slice by allocating memory using the current context allocator,
// NUL terminating it, and copying over its data.
char *ext_ss_to_cstr(Ext_StringSlice ss);
// Creates a cstring from the string slice by allocating memory using the temp allocator,
// NUL terminating it, and copying over its data.
char *ext_ss_to_cstr_temp(Ext_StringSlice ss);
// Creates a cstring from the string slice by allocating memory using the provided allocator,
// NUL terminating it, and copying over its data.
char *ext_ss_to_cstr_alloc(Ext_StringSlice ss, Ext_Allocator *a);
// Returns the filename component of `path` (after the last separator)
Ext_StringSlice ext_ss_basename(Ext_StringSlice path);
// Returns the directory component of `path` (before the last separator), or empty if none
Ext_StringSlice ext_ss_dirname(Ext_StringSlice path);
// Returns the file extension (including the dot), or empty if none
Ext_StringSlice ext_ss_extension(Ext_StringSlice path);
// Appends a path component to the buffer, inserting a separator if needed
void ext_sb_append_path(Ext_StringBuffer *sb, Ext_StringSlice component);
// Like `sb_append_path`, but takes a C string component
void ext_sb_append_path_cstr(Ext_StringBuffer *sb, const char *component);

// -----------------------------------------------------------------------------
// SECTION: IO
//

#ifndef EXTLIB_NO_STD
typedef enum {
    EXT_FILE_ERR = -1,
    EXT_FILE_REGULAR,
    EXT_FILE_DIR,
    EXT_FILE_SYMLINK,
    EXT_FILE_OTHER,
} Ext_FileType;

typedef struct {
    char **items;
    size_t size, capacity;
    Ext_Allocator *allocator;
} Ext_Paths;
void ext_free_paths(Ext_Paths *paths);

// Reads an entire file into the provided string buffer. Retuns true on succes, false on failure.
bool ext_read_file(const char *path, Ext_StringBuffer *sb);
// Writes `data` into the file at `path`. The file is overwritten if it exists. Returns true on
// success, false on failure
bool ext_write_file(const char *path, const void *data, size_t size);
// Reads a line from a file into the provided string buffer. Returns 1 if there are more lines to be
// read, 0 if there aren't or -1 on error.
int ext_read_line(FILE *f, Ext_StringBuffer *sb);
// Reads a directory into the `paths` array. Returns true on success, false on failure
bool ext_read_dir(const char *path, Ext_Paths *paths);
// Creates a directory. Does nothing if the directory already exists
bool ext_create_dir(const char *path);
// Deletes a directory recursively (i.e. even if not empty)
bool ext_delete_dir_recursively(const char *path);
// Returns the type of the file at `path`. On error returns EXT_FILE_ERR (-1).
Ext_FileType ext_get_file_type(const char *path);
// Renames a file (or directory)
bool ext_rename_file(const char *old_path, const char *new_path);
// Deletes a file (or empty directory)
bool ext_delete_file(const char *path);
// Gets the current working directory using the current context allocator. Returns NULL on failure.
char *ext_get_cwd(void);
// Gets the current working directory using the temporary allocator. Returns NULL on failure.
char *ext_get_cwd_temp(void);
// Gets the current working directory using the provided allocator. Returns NULL on failure.
char *ext_get_cwd_alloc(Ext_Allocator *a);
// Sets the current working directory
bool ext_set_cwd(const char *cwd);
// Transforms `path` into an absolute path. Allocates using the provided allocator.
char *ext_get_abs_path_alloc(const char *path, Ext_Allocator *a);
// Transforsms `path` into an absolute path. Allocates using the temp allocator.
char *ext_get_abs_path_temp(const char *path);
// Transforsms `path` into an absolute path. Allocates using the current context allocator.
char *ext_get_abs_path(const char *path);
// Executes the given command using the system shell. Returns the exit code of the process.
int ext_cmd(const char *cmd);
// Executes the given command using the system shell, appending its stdout into the provided string
// buffer. Returns the exit code of the process
int ext_cmd_read(const char *cmd, Ext_StringBuffer *sb);
// Executes the given command using the system shell, writing into its stdin the data provided in
// `data`. Returns the exit code of the process.
int ext_cmd_write(const char *cmd, const void *data, size_t size);
#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: Hashmap
//
// Generic typesafe hashmap.
// The hashmap is implemented using open-adressing and linear probing. Order of the keys in the
// map is random, so you shouldn't assume any order during iteration.
//
// The hashmap integrates with the `Allocator` interface and the context to support custom
// allocators for its backing entry and hashes array.
//
// USAGE
// ```c
// typedef struct {
//     int key;
//     int value;
// } IntEntry;
//
// typedef struct {
//     IntEntry *entries;
//     size_t *hashes;
//     size_t size, capacity;
//     Allocator *allocator;
// } IntMap;
//
// IntHashMap map = {0};
// hmap_put(&map, 1, 10);
//
// IntEntry* e;
// hmap_get(&map, 1, &e);
// if(e != NULL) { // Found!
//     printf("key = %d value = %d", e->key, e->value);
// }
//
// hmap_free(&map);
//
// // custom allocator
// IntHashMap map = {0};
// map.allocator = &temp_allocator.base;
// // ... put/get/deletes
// temp_reset(&map);
// ```

// Read as: size * 0.75, i.e. a load factor of 75%
// This is basically doing:
//   size / 2 + size / 4 = (3 * size) / 4
#define EXT_HMAP_MAX_ENTRY_LOAD(size) (((size) >> 1) + ((size) >> 2))

// Puts an entry into the hashmap.
// `hash_fn` is expected to be a function (or function-like macro)
// that takes a key by pointer and returns an hash of it. `cmp_fn` is also expected to be a
// function (or function-like macro) that takes two keys by pointer and compares them, returning
// 0 if they're euqal, -1 if a is less than b, 1 if a is greater than b.
//
// You probably want to use the non-ex version of this function (ext_hmap_put, ext_hmap_put_cstr
// or ext_hmap_put_ss) unless you specifically need to customize the way entries are hashed or
// compared.
#define ext_hmap_put_ex(hmap, entry_key, entry_val, hash_fn, cmp_fn)                             \
    do {                                                                                         \
        if((hmap)->size >= EXT_HMAP_MAX_ENTRY_LOAD((hmap)->capacity + 1)) {                      \
            ext_hmap_grow_((void **)&(hmap)->entries, sizeof(*(hmap)->entries), &(hmap)->hashes, \
                           &(hmap)->capacity, (Ext_Allocator **)&(hmap)->allocator);             \
            ext_hmap_tombs_(hmap) = (hmap)->size;                                                \
        }                                                                                        \
        ext_hmap_tmp_(hmap).key = (entry_key);                                                   \
        ext_hmap_tmp_(hmap).value = (entry_val);                                                 \
        size_t hash = hash_fn(&ext_hmap_tmp_(hmap).key);                                         \
        if(hash < 2) hash += 2;                                                                  \
        ext_hmap_find_index_(hmap, &ext_hmap_tmp_(hmap).key, hash, cmp_fn);                      \
        if(!EXT_HMAP_IS_VALID((hmap)->hashes[idx_])) {                                           \
            (hmap)->size++;                                                                      \
            if(!EXT_HMAP_IS_TOMB((hmap)->hashes[idx_])) ext_hmap_tombs_(hmap)++;                 \
        }                                                                                        \
        (hmap)->entries[idx_] = ext_hmap_tmp_(hmap);                                             \
        (hmap)->hashes[idx_] = hash;                                                             \
    } while(0)

// Gets an entry from the hashmap.
// The retrieved entry is put in `out` if found, otherwise `out` is set to NULL.
// `hash_fn` is expected to be a function (or function-like macro) that takes a key by pointer
// and returns an hash of it. `cmp_fn` is also expected to be a function (or function-like
// macro) that takes two keys by pointer and compares them, returning 0 if they're euqal, -1 if
// a is less than b, 1 if a is greater than b.
//
// You probably want to use the non-ex version of this function (ext_hmap_get, ext_hmap_get_cstr
// or ext_hmap_get_ss) unless you specifically need to customize the way entries are hashed or
// compared.
#define ext_hmap_get_ex(hmap, entry_key, out, hash_fn, cmp_fn)              \
    do {                                                                    \
        if(!(hmap)->size) {                                                 \
            *(out) = NULL;                                                  \
            break;                                                          \
        }                                                                   \
        ext_hmap_tmp_(hmap).key = (entry_key);                              \
        size_t hash = hash_fn(&ext_hmap_tmp_(hmap).key);                    \
        if(hash < 2) hash += 2;                                             \
        ext_hmap_find_index_(hmap, &ext_hmap_tmp_(hmap).key, hash, cmp_fn); \
        if(EXT_HMAP_IS_VALID((hmap)->hashes[idx_])) {                       \
            *(out) = &(hmap)->entries[idx_];                                \
        } else {                                                            \
            *(out) = NULL;                                                  \
        }                                                                   \
    } while(0)

// Gets an entry from the hashmap, creating a new one in if it is not found.
// The retrieved (or newly created) entry is put in `out`.
// `hash_fn` is expected to be a function (or function-like macro) that takes a key by pointer
// and returns an hash of it. `cmp_fn` is also expected to be a function (or function-like
// macro) that takes two keys by pointer and compares them, returning 0 if they're euqal, -1 if
// a is less than b, 1 if a is greater than b.
//
// You probably want to use the non-ex version of this function (ext_hmap_get, ext_hmap_get_cstr
// or ext_hmap_get_ss) unless you specifically need to customize the way entries are hashed or
// compared.
#define ext_hmap_get_default_ex(hmap, entry_key, entry_val, out, hash_fn, cmp_fn)                \
    do {                                                                                         \
        if((hmap)->size >= EXT_HMAP_MAX_ENTRY_LOAD((hmap)->capacity + 1)) {                      \
            ext_hmap_grow_((void **)&(hmap)->entries, sizeof(*(hmap)->entries), &(hmap)->hashes, \
                           &(hmap)->capacity, (Ext_Allocator **)&(hmap)->allocator);             \
            ext_hmap_tombs_(hmap) = (hmap)->size;                                                \
        }                                                                                        \
        ext_hmap_tmp_(hmap).key = (entry_key);                                                   \
        ext_hmap_tmp_(hmap).value = (entry_val);                                                 \
        size_t hash = hash_fn(&ext_hmap_tmp_(hmap).key);                                         \
        if(hash < 2) hash += 2;                                                                  \
        ext_hmap_find_index_(hmap, &ext_hmap_tmp_(hmap).key, hash, cmp_fn);                      \
        if(!EXT_HMAP_IS_VALID((hmap)->hashes[idx_])) {                                           \
            (hmap)->size++;                                                                      \
            if(!EXT_HMAP_IS_TOMB((hmap)->hashes[idx_])) ext_hmap_tombs_(hmap)++;                 \
            (hmap)->entries[idx_] = ext_hmap_tmp_(hmap);                                         \
            (hmap)->hashes[idx_] = hash;                                                         \
        }                                                                                        \
        *(out) = &(hmap)->entries[idx_];                                                         \
    } while(0)

// Deletes an entry from the hashmap.
// `hash_fn` is expected to be a function (or function-like macro) that takes the entry and
// returns an hash of it. `cmp_fn` is also expected to be a function (or function-like macro)
// that takes two entries and compares them, returning 0 if they're euqal, -1 if a is less than
// b, 1 if a is greater than b.
//
// You probably want to use the non-ex version of this function (ext_hmap_delete,
// ext_hmap_delete_cstr or ext_hmap_delete_ss) unless you specifically need to customize the way
// entries are hashed or compared.
#define ext_hmap_delete_ex(hmap, entry_key, hash_fn, cmp_fn)                \
    do {                                                                    \
        if(!(hmap)->size) break;                                            \
        ext_hmap_tmp_(hmap).key = (entry_key);                              \
        size_t hash = hash_fn(&ext_hmap_tmp_(hmap).key);                    \
        if(hash < 2) hash += 2;                                             \
        ext_hmap_find_index_(hmap, &ext_hmap_tmp_(hmap).key, hash, cmp_fn); \
        if(EXT_HMAP_IS_VALID((hmap)->hashes[idx_])) {                       \
            (hmap)->hashes[idx_] = EXT_HMAP_TOMB_MARK;                      \
            (hmap)->size--;                                                 \
        }                                                                   \
    } while(0)

// Puts an entry into the hashmap. keys are compared with `memcmp`.
#define ext_hmap_put(hmap, entry_key, entry_val) \
    ext_hmap_put_ex(hmap, entry_key, entry_val, ext_hmap_hash_bytes_, ext_hmap_memcmp_)
// Gets an entry from the hashmap. keys are compared with `memcmp`.
#define ext_hmap_get(hmap, entry_key, out) \
    ext_hmap_get_ex(hmap, entry_key, out, ext_hmap_hash_bytes_, ext_hmap_memcmp_)
// Gets an entry from the hashmap, creating a new one if not found. keys are compared with
// `memcmp`.
#define ext_hmap_get_default(hmap, entry_key, entry_val, out) \
    ext_hmap_get_default_ex(hmap, entry_key, entry_val, out, ext_hmap_hash_bytes_, ext_hmap_memcmp_)
// Deletes an entry from the hashmap. keys are compared with `memcmp`.
#define ext_hmap_delete(hmap, entry_key) \
    ext_hmap_delete_ex(hmap, entry_key, ext_hmap_hash_bytes_, ext_hmap_memcmp_)

// Puts an entry into the hashmap. keys are compared with `strcmp`.
#define ext_hmap_put_cstr(hmap, entry_key, entry_val) \
    ext_hmap_put_ex(hmap, entry_key, entry_val, ext_hmap_hash_cstr_, ext_hmap_strcmp_)
// Gets an entry from the hashmap. keys are compared with `strcmp`.
#define ext_hmap_get_cstr(hmap, entry_key, out) \
    ext_hmap_get_ex(hmap, entry_key, out, ext_hmap_hash_cstr_, ext_hmap_strcmp_)
// Gets an entry from the hashmap, creating a new one if not found. keys are compared with
// `strcmp`.
#define ext_hmap_get_default_cstr(hmap, entry_key, entry_val, out) \
    ext_hmap_get_default_ex(hmap, entry_key, entry_val, out, ext_hmap_hash_cstr_, ext_hmap_strcmp_)
// Deletes an entry from the hashmap. keys are compared with `strcmp`.
#define ext_hmap_delete_cstr(hmap, entry_key) \
    ext_hmap_delete_ex(hmap, entry_key, ext_hmap_hash_cstr_, ext_hmap_strcmp_)

// Puts an entry into the hashmap. keys are compared with `ss_cmp`.
#define ext_hmap_put_ss(hmap, entry_key, entry_val) \
    ext_hmap_put_ex(hmap, entry_key, entry_val, ext_hmap_hash_ss_, ext_hmap_sscmp_)
// Gets an entry from the hashmap. keys are compared with `ext_ss_cmp`.
#define ext_hmap_get_ss(hmap, entry_key, out) \
    ext_hmap_get_ex(hmap, entry_key, out, ext_hmap_hash_ss_, ext_hmap_sscmp_)
// Gets an entry from the hashmap, creating a new one if not found. keys are compared with
// `ss_cmp`.
#define ext_hmap_get_default_ss(hmap, entry_key, entry_val, out) \
    ext_hmap_get_default_ex(hmap, entry_key, entry_val, out, ext_hmap_hash_ss_, ext_hmap_sscmp_)
// Delets an entry from the hashmap. keys are compared with `ss_cmp`.
#define ext_hmap_delete_ss(hmap, entry_key) \
    ext_hmap_delete_ex(hmap, entry_key, ext_hmap_hash_ss_, ext_hmap_sscmp_)

// Clears the hashmap
#define ext_hmap_clear(hmap)                                                         \
    do {                                                                             \
        memset((hmap)->hashes, 0, sizeof(*(hmap)->hashes) * ((hmap)->capacity + 1)); \
        (hmap)->size = 0;                                                            \
    } while(0)

// Frees and clears the hashmap
#define ext_hmap_free(hmap)                                                                   \
    do {                                                                                      \
        if((hmap)->entries) {                                                                 \
            size_t sz = ((hmap)->capacity + 2) * sizeof(*(hmap)->entries);                    \
            size_t pad = EXT_ALIGN_PAD(sz, sizeof(*(hmap)->hashes));                          \
            size_t totalsz = sz + pad + sizeof(*(hmap)->hashes) * ((hmap)->capacity + 2);     \
            ext_allocator_free((Ext_Allocator *)(hmap)->allocator, (hmap)->entries, totalsz); \
        }                                                                                     \
        memset((hmap), 0, sizeof(*(hmap)));                                                   \
    } while(0)

// Iterate all entries in the hashmap
//
// USAGE
// ```c
// ext_hmap_foreach(IntEntry, it, &map) {
//     printf("key = %s value = %s", it->key, it->value);
// }
// ```
#define ext_hmap_foreach(T, it, hmap)                                       \
    for(T *it = ext_hmap_begin(hmap), *end = ext_hmap_end(hmap); it != end; \
        it = ext_hmap_next(hmap, it))

// Manually iterate all entries in the hashmap
//
// USAGE
// ```c
// for(IntEntry* it = hmap_begin(&map); it != hmap_end(&map); it = hmap_next(&map)) {
//     printf("key = %s value = %s", it->key, it->value);
// }
// ```
#define ext_hmap_end(hmap) \
    ext_hmap_end_((hmap)->entries, (hmap)->capacity, sizeof(*(hmap)->entries))
#define ext_hmap_begin(hmap) \
    ext_hmap_begin_((hmap)->entries, (hmap)->hashes, (hmap)->capacity, sizeof(*(hmap)->entries))
#define ext_hmap_next(hmap, it) \
    ext_hmap_next_((hmap)->entries, (hmap)->hashes, it, (hmap)->capacity, sizeof(*(hmap)->entries))

#ifndef EXT_HMAP_INIT_CAPACITY
#define EXT_HMAP_INIT_CAPACITY 8
#endif  // EXT_HMAP_INIT_CAPACITY

EXT_STATIC_ASSERT(((EXT_HMAP_INIT_CAPACITY) & (EXT_HMAP_INIT_CAPACITY - 1)) == 0,
                  "hashmap initial capacity must be a power of two");

// -----------------------------------------------------------------------------
// Private hashmap implementation

#define EXT_HMAP_TMP_SLOT    0
#define EXT_HMAP_EMPTY_MARK  0
#define EXT_HMAP_TOMB_MARK   1
#define EXT_HMAP_IS_TOMB(h)  ((h) == EXT_HMAP_TOMB_MARK)
#define EXT_HMAP_IS_EMPTY(h) ((h) == EXT_HMAP_EMPTY_MARK)
#define EXT_HMAP_IS_VALID(h) (!EXT_HMAP_IS_EMPTY(h) && !EXT_HMAP_IS_TOMB(h))

void ext_hmap_grow_(void **entries, size_t entries_sz, size_t **hashes, size_t *cap,
                    Ext_Allocator **a);

#define ext_hmap_tmp_(map)   ((map)->entries[EXT_HMAP_TMP_SLOT])
#define ext_hmap_tombs_(map) ((map)->hashes[EXT_HMAP_TMP_SLOT])

#define ext_hmap_find_index_(map, entry_key, hash, cmp_fn)                                     \
    size_t idx_ = 0;                                                                           \
    {                                                                                          \
        size_t i_ = ((hash) & (map)->capacity);                                                \
        bool tomb_found_ = false;                                                              \
        size_t tomb_idx_ = 0;                                                                  \
        for(;;) {                                                                              \
            size_t buck = (map)->hashes[i_ + 1];                                               \
            if(!EXT_HMAP_IS_VALID(buck)) {                                                     \
                if(EXT_HMAP_IS_EMPTY(buck)) {                                                  \
                    idx_ = tomb_found_ ? tomb_idx_ : i_ + 1;                                   \
                    break;                                                                     \
                } else if(!tomb_found_) {                                                      \
                    tomb_found_ = true;                                                        \
                    tomb_idx_ = i_ + 1;                                                        \
                }                                                                              \
            } else if(buck == hash && cmp_fn((entry_key), &(map)->entries[i_ + 1].key) == 0) { \
                idx_ = i_ + 1;                                                                 \
                break;                                                                         \
            }                                                                                  \
            i_ = ((i_ + 1) & (map)->capacity);                                                 \
        }                                                                                      \
    }

#define ext_hmap_hash_bytes_(k)  ext_hash_bytes_((k), sizeof(*(k)))
#define ext_hmap_hash_cstr_(k)   ext_hash_cstr_(*(k))
#define ext_hmap_hash_ss_(k)     ext_hash_bytes_((k)->data, (k)->size)
#define ext_hmap_memcmp_(k1, k2) memcmp((k1), (k2), sizeof(*(k1)))
#define ext_hmap_strcmp_(k1, k2) strcmp(*(k1), *(k2))
#define ext_hmap_sscmp_(k1, k2)  ext_ss_cmp(*(k1), *(k2))

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif  // __GNUC__

static inline void *ext_hmap_end_(const void *entries, size_t cap, size_t sz) {
    return entries ? (char *)entries + (cap + 2) * sz : NULL;
}

static inline void *ext_hmap_begin_(const void *entries, const size_t *hashes, size_t cap,
                                    size_t sz) {
    if(!entries) return NULL;
    for(size_t i = 1; i <= cap + 1; i++) {
        if(EXT_HMAP_IS_VALID(hashes[i])) {
            return (char *)entries + i * sz;
        }
    }
    return ext_hmap_end_(entries, cap, sz);
}

static inline void *ext_hmap_next_(const void *entries, const size_t *hashes, const void *it,
                                   size_t cap, size_t sz) {
    size_t curr = ((char *)it - (char *)entries) / sz;
    for(size_t idx = curr + 1; idx <= cap + 1; idx++) {
        if(EXT_HMAP_IS_VALID(hashes[idx])) {
            return (char *)entries + idx * sz;
        }
    }
    return ext_hmap_end_(entries, cap, sz);
}

// -----------------------------------------------------------------------------
// From stb_ds.h
//
// Copyright (c) 2019 Sean Barrett
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define EXT_SIZET_BITS           ((sizeof(size_t)) * CHAR_BIT)
#define EXT_ROTATE_LEFT(val, n)  (((val) << (n)) | ((val) >> (EXT_SIZET_BITS - (n))))
#define EXT_ROTATE_RIGHT(val, n) (((val) >> (n)) | ((val) << (EXT_SIZET_BITS - (n))))

static inline size_t ext_hash_cstr_(const char *str) {
    const size_t seed = 2147483647;
    size_t hash = seed;
    while(*str) hash = EXT_ROTATE_LEFT(hash, 9) + (unsigned char)*str++;
    // Thomas Wang 64-to-32 bit mix function, hopefully also works in 32 bits
    hash ^= seed;
    hash = (~hash) + (hash << 18);
    hash ^= hash ^ EXT_ROTATE_RIGHT(hash, 31);
    hash = hash * 21;
    hash ^= hash ^ EXT_ROTATE_RIGHT(hash, 11);
    hash += (hash << 6);
    hash ^= EXT_ROTATE_RIGHT(hash, 22);
    return hash + seed;
}

#ifdef EXT_SIPHASH_2_4
#define EXT_SIPHASH_C_ROUNDS 2
#define EXT_SIPHASH_D_ROUNDS 4
typedef int EXT_SIPHASH_2_4_can_only_be_used_in_64_bit_builds[sizeof(size_t) == 8 ? 1 : -1];
#endif

#ifndef EXT_SIPHASH_C_ROUNDS
#define EXT_SIPHASH_C_ROUNDS 1
#endif
#ifndef EXT_SIPHASH_D_ROUNDS
#define EXT_SIPHASH_D_ROUNDS 1
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)  // conditional expression is constant, for
                                 // do..while(0) and sizeof()==
#endif

static size_t ext_siphash_bytes_(const void *p, size_t len, size_t seed) {
    unsigned char *d = (unsigned char *)p;
    size_t i, j;
    size_t v0, v1, v2, v3, data;

    // hash that works on 32- or 64-bit registers without knowing which we have
    // (computes different results on 32-bit and 64-bit platform)
    // derived from siphash, but on 32-bit platforms very different as it uses 4
    // 32-bit state not 4 64-bit
    v0 = ((((size_t)0x736f6d65 << 16) << 16) + 0x70736575) ^ seed;
    v1 = ((((size_t)0x646f7261 << 16) << 16) + 0x6e646f6d) ^ ~seed;
    v2 = ((((size_t)0x6c796765 << 16) << 16) + 0x6e657261) ^ seed;
    v3 = ((((size_t)0x74656462 << 16) << 16) + 0x79746573) ^ ~seed;

#ifdef EXT_TEST_SIPHASH_2_4
    // hardcoded with key material in the siphash test vectors
    v0 ^= 0x0706050403020100ull ^ seed;
    v1 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
    v2 ^= 0x0706050403020100ull ^ seed;
    v3 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
#endif

#define EXT_SIPROUND()                                \
    do {                                              \
        v0 += v1;                                     \
        v1 = EXT_ROTATE_LEFT(v1, 13);                 \
        v1 ^= v0;                                     \
        v0 = EXT_ROTATE_LEFT(v0, EXT_SIZET_BITS / 2); \
        v2 += v3;                                     \
        v3 = EXT_ROTATE_LEFT(v3, 16);                 \
        v3 ^= v2;                                     \
        v2 += v1;                                     \
        v1 = EXT_ROTATE_LEFT(v1, 17);                 \
        v1 ^= v2;                                     \
        v2 = EXT_ROTATE_LEFT(v2, EXT_SIZET_BITS / 2); \
        v0 += v3;                                     \
        v3 = EXT_ROTATE_LEFT(v3, 21);                 \
        v3 ^= v0;                                     \
    } while(0)

    for(i = 0; i + sizeof(size_t) <= len; i += sizeof(size_t), d += sizeof(size_t)) {
        data = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
        data |= (size_t)(d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24))
                << 16 << 16;  // discarded if size_t == 4

        v3 ^= data;
        for(j = 0; j < EXT_SIPHASH_C_ROUNDS; ++j) EXT_SIPROUND();
        v0 ^= data;
    }
    data = len << (EXT_SIZET_BITS - 8);
    switch(len - i) {
    case 7:
        data |= ((size_t)d[6] << 24) << 24;  // fall through
    case 6:
        data |= ((size_t)d[5] << 20) << 20;  // fall through
    case 5:
        data |= ((size_t)d[4] << 16) << 16;  // fall through
    case 4:
        data |= (d[3] << 24);  // fall through
    case 3:
        data |= (d[2] << 16);  // fall through
    case 2:
        data |= (d[1] << 8);  // fall through
    case 1:
        data |= d[0];  // fall through
    case 0:
        break;
    }
    v3 ^= data;
    for(j = 0; j < EXT_SIPHASH_C_ROUNDS; ++j) EXT_SIPROUND();
    v0 ^= data;
    v2 ^= 0xff;
    for(j = 0; j < EXT_SIPHASH_D_ROUNDS; ++j) EXT_SIPROUND();

#ifdef EXT_SIPHASH_2_4
    return v0 ^ v1 ^ v2 ^ v3;
#else
    return v1 ^ v2 ^ v3;  // slightly stronger since v0^v3 in above cancels out
                          // final round operation? I tweeted at the authors of
                          // SipHash about this but they didn't reply
#endif
}

static inline size_t ext_hash_bytes_(const void *p, size_t len) {
    const size_t seed = 2147483647;
#ifdef EXT_SIPHASH_2_4
    return stbds_siphash_bytes(p, len, seed);
#else
    unsigned char *d = (unsigned char *)p;

    if(len == 4) {
        unsigned int hash = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
        // HASH32-BB  Bob Jenkin's presumably-accidental version of Thomas Wang hash
        // with rotates turned into shifts. Note that converting these back to
        // rotates makes it run a lot slower, presumably due to collisions, so I'm
        // not really sure what's going on.
        hash ^= seed;
        hash = (hash ^ 61) ^ (hash >> 16);
        hash = hash + (hash << 3);
        hash = hash ^ (hash >> 4);
        hash = hash * 0x27d4eb2d;
        hash ^= seed;
        hash = hash ^ (hash >> 15);
        return (((size_t)hash << 16 << 16) | hash) ^ seed;
    } else if(len == 8 && sizeof(size_t) == 8) {
        size_t hash = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
        hash |= (size_t)(d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24))
                << 16 << 16;  // avoid warning if size_t == 4
        hash ^= seed;
        hash = (~hash) + (hash << 21);
        hash ^= EXT_ROTATE_RIGHT(hash, 24);
        hash *= 265;
        hash ^= EXT_ROTATE_RIGHT(hash, 14);
        hash ^= seed;
        hash *= 21;
        hash ^= EXT_ROTATE_RIGHT(hash, 28);
        hash += (hash << 31);
        hash = (~hash) + (hash << 18);
        return hash;
    } else {
        return ext_siphash_bytes_(p, len, seed);
    }
#endif
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif  // _MSC_VER
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__

// End of stbds.h
// -----------------------------------------------------------------------------

#define ext__return_tox_(a, b, c, d, ...) d
#define ext__return_to1_(result_)         ext__return_to3_(result_, exit, res)
#define ext__return_to2_(result_, label_) ext__return_to3_(result_, label_, res)
#define ext__return_to3_(result_, label_, res_var_) \
    do {                                            \
        res_var_ = result_;                         \
        goto label_;                                \
    } while(0)

#ifdef EXTLIB_IMPL
// -----------------------------------------------------------------------------
// SECTION: Logging
//
void ext_log(Ext_LogLevel lvl, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ext_logvf(lvl, fmt, ap);
    va_end(ap);
}

void ext_logvf(Ext_LogLevel lvl, const char *fmt, va_list ap) {
    if(!ext_context->log_fn || lvl == EXT_NO_LOGGING || lvl < ext_context->log_level) return;
    ext_context->log_fn(lvl, ext_context->log_data, fmt, ap);
}

#ifndef EXTLIB_NO_STD
static void ext_default_log(Ext_LogLevel lvl, void *data, const char *fmt, va_list ap) {
    (void)data;
    switch(lvl) {
    case EXT_DEBUG:
        fprintf(stdout, "[DEBUG] ");
        break;
    case EXT_INFO:
        fprintf(stdout, "[INFO] ");
        break;
    case EXT_WARNING:
        fprintf(stdout, "[WARNING] ");
        break;
    case EXT_ERROR:
        fprintf(stderr, "[ERROR] ");
        break;
    case EXT_NO_LOGGING:
        EXT_UNREACHABLE();
    }
    vfprintf(lvl == EXT_ERROR ? stderr : stdout, fmt, ap);
    fprintf(lvl == EXT_ERROR ? stderr : stdout, "\n");
}
#endif

// -----------------------------------------------------------------------------
// SECTION: Context
//
EXT_TLS Ext_Context *ext_context = &(Ext_Context){
    .alloc = &ext_default_allocator.base,
    .log_level = EXT_INFO,
    .log_data = NULL,
#ifndef EXTLIB_NO_STD
    .log_fn = &ext_default_log,
#else
    .log_fn = NULL,
#endif
};

void ext_push_context(Ext_Context *ctx) {
    ctx->prev = ext_context;
    ext_context = ctx;
}

Ext_Context *ext_pop_context(void) {
    EXT_ASSERT(ext_context->prev, "Trying to pop default allocator");
    Ext_Context *old_ctx = ext_context;
    ext_context = old_ctx->prev;
    return old_ctx;
}

// -----------------------------------------------------------------------------
// SECTION: Allocators
//

extern inline void *ext_allocator_alloc(Ext_Allocator *a, size_t size);
extern inline void *ext_allocator_realloc(Ext_Allocator *a, void *ptr, size_t old_sz,
                                          size_t new_sz);
extern inline void ext_allocator_free(Ext_Allocator *a, void *ptr, size_t size);

extern inline void *ext_alloc(size_t size);
extern inline void *ext_realloc(void *ptr, size_t old_sz, size_t new_sz);
extern inline void ext_free(void *ptr, size_t size);

extern inline char *ext_strdup(const char *s);
extern inline void *ext_memdup(const void *mem, size_t size);

char *ext_allocator_strdup(Ext_Allocator *a, const char *s) {
    size_t len = strlen(s);
    char *res = a->alloc(a, len + 1);
    memcpy(res, s, len);
    res[len] = '\0';
    return res;
}

void *ext_allocator_memdup(Ext_Allocator *a, const void *mem, size_t size) {
    return memcpy(a->alloc(a, size), mem, size);
}

#ifdef EXTLIB_WASM
extern char __heap_base[];
static void *ext_heap_start = (void *)__heap_base;
#endif  // EXTLIB_WASM

#ifndef EXT_DEFAULT_ALIGNMENT
#define EXT_DEFAULT_ALIGNMENT 16
#endif  // EXT_DEFAULT_ALIGNMENT
EXT_STATIC_ASSERT(((EXT_DEFAULT_ALIGNMENT) & ((EXT_DEFAULT_ALIGNMENT)-1)) == 0,
                  "default alignment must be a power of 2");

#ifndef EXT_DEFAULT_TEMP_SIZE
#define EXT_DEFAULT_TEMP_SIZE EXT_MiB(256)
#endif

static void *ext_default_alloc(Ext_Allocator *a, size_t size) {
    (void)a;
#ifndef EXTLIB_NO_STD
    void *mem = malloc(size);
    EXT_ASSERT(mem, "out of memory");
    return mem;
#elif defined(EXTLIB_WASM)
    size = EXT_ALIGN_UP(size, EXT_DEFAULT_ALIGNMENT);
    char *mem_end = (char *)(__builtin_wasm_memory_size(0) << 16);
    if((char *)ext_heap_start + size > mem_end) {
        size_t pages = (size + 0xFFFFU) >> 16;  // round up
        ext_heap_start = (void *)__builtin_wasm_memory_grow(0, pages);
    }
    void *mem = ext_heap_start;
    ext_heap_start = (char *)ext_heap_start + size;
    return mem;
#else
    (void)size;
    return NULL;
#endif
}

static void *ext_default_realloc(Ext_Allocator *a, void *ptr, size_t old_size, size_t new_size) {
#ifndef EXTLIB_NO_STD
    (void)a;
    (void)old_size;
    void *mem = realloc(ptr, new_size);
    EXT_ASSERT(mem, "out of memory");
    return mem;
#elif defined EXTLIB_WASM
    void *mem = ext_default_alloc(a, new_size);
    memcpy(mem, ptr, old_size);
    return mem;
#else
    (void)ptr;
    (void)new_size;
    return NULL;
#endif
}

static void ext_default_free(Ext_Allocator *a, void *ptr, size_t size) {
    (void)a;
    (void)size;
#ifndef EXTLIB_NO_STD
    free(ptr);
#else
    (void)ptr;
#endif
}

Ext_DefaultAllocator ext_default_allocator = {
    {
        .alloc = ext_default_alloc,
        .realloc = ext_default_realloc,
        .free = ext_default_free,
    },
};

// -----------------------------------------------------------------------------
// SECTION: Temporary allocator
//
static void *ext_temp_alloc_wrap(Ext_Allocator *a, size_t size);
static void *ext_temp_realloc_wrap(Ext_Allocator *a, void *ptr, size_t old_size, size_t new_size);
static void ext_temp_free_wrap(Ext_Allocator *a, void *ptr, size_t size);

EXT_ALIGNAS(EXT_DEFAULT_ALIGNMENT) static char ext_temp_mem[EXT_DEFAULT_TEMP_SIZE];
EXT_TLS Ext_TempAllocator ext_temp_allocator = {
    {.alloc = ext_temp_alloc_wrap, .realloc = ext_temp_realloc_wrap, .free = ext_temp_free_wrap},
    .start = ext_temp_mem,
    .end = ext_temp_mem + EXT_DEFAULT_TEMP_SIZE,
    .mem_size = EXT_DEFAULT_TEMP_SIZE,
    .mem = ext_temp_mem,
};

static void *ext_temp_alloc_wrap(Ext_Allocator *a, size_t size) {
    (void)a;
    return ext_temp_alloc(size);
}

static void *ext_temp_realloc_wrap(Ext_Allocator *a, void *ptr, size_t old_size, size_t new_size) {
    (void)a;
    return ext_temp_realloc(ptr, old_size, new_size);
}

static void ext_temp_free_wrap(Ext_Allocator *a, void *ptr, size_t size) {
    (void)a;
    (void)ptr;
    (void)size;
    // No-op, temp allocator does not free memory
}

void ext_temp_set_mem(void *mem, size_t size) {
    ext_temp_allocator.mem_size = size;
    ext_temp_allocator.mem = mem;
    ext_temp_reset();
}

void *ext_temp_alloc(size_t size) {
    size = EXT_ALIGN_UP(size, EXT_DEFAULT_ALIGNMENT);
    size_t available = ext_temp_allocator.end - ext_temp_allocator.start;
    if(available < size) {
#ifndef EXTLIB_NO_STD
        ext_log(EXT_ERROR,
                "%s:%d: temp allocation failed: %zu bytes requested, %zu bytes "
                "available",
                __FILE__, __LINE__, size, available);
        abort();
#else
        EXT_ASSERT(false, "temp allocation failed");
#endif
    }
    void *p = ext_temp_allocator.start;
    ext_temp_allocator.start += size;
    return p;
}

void *ext_temp_realloc(void *ptr, size_t old_size, size_t new_size) {
    size_t aligned_old = EXT_ALIGN_UP(old_size, EXT_DEFAULT_ALIGNMENT);
    // Reallocating last allocated memory, can shrink in-place
    if(ext_temp_allocator.start - aligned_old == ptr) {
        ext_temp_allocator.start -= aligned_old;
        return ext_temp_alloc(new_size);
    } else if(new_size > old_size) {
        void *new_ptr = ext_temp_alloc(new_size);
        memcpy(new_ptr, ptr, old_size);
        return new_ptr;
    } else {
        return ptr;
    }
}

size_t ext_temp_available(void) {
    return ext_temp_allocator.end - ext_temp_allocator.start;
}

void ext_temp_reset(void) {
    char *mem = ext_temp_allocator.mem;
    ext_temp_allocator.start = mem + EXT_ALIGN_PAD(mem, EXT_DEFAULT_ALIGNMENT);
    ext_temp_allocator.end = mem + ext_temp_allocator.mem_size;
}

void *ext_temp_checkpoint(void) {
    return ext_temp_allocator.start;
}

void ext_temp_rewind(void *checkpoint) {
    ext_temp_allocator.start = checkpoint;
}

char *ext_temp_strdup(const char *s) {
    return ext_allocator_strdup(&ext_temp_allocator.base, s);
}

void *ext_temp_memdup(void *mem, size_t size) {
    return ext_allocator_memdup(&ext_temp_allocator.base, mem, size);
}

#ifndef EXTLIB_NO_STD
char *ext_temp_sprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *res = ext_temp_vsprintf(fmt, ap);
    va_end(ap);
    return res;
}

char *ext_temp_vsprintf(const char *fmt, va_list ap) {
    va_list cpy;
    va_copy(cpy, ap);
    int n = vsnprintf(NULL, 0, fmt, cpy);
    va_end(cpy);

    EXT_ASSERT(n >= 0, "error in vsnprintf");
    char *res = ext_temp_alloc(n + 1);

    va_copy(cpy, ap);
    vsnprintf(res, n + 1, fmt, cpy);
    va_end(cpy);

    return res;
}
#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: Arena allocator
//
#ifndef EXT_ARENA_PAGE_SZ
#define EXT_ARENA_PAGE_SZ EXT_KiB(8)
#endif  // EXT_ARENA_PAGE_SZ

static Ext_ArenaPage *ext_arena_new_page(Ext_Arena *arena, size_t requested_size) {
    size_t header_sz = sizeof(Ext_ArenaPage) + (arena->alignment - 1);
    size_t actual_size = requested_size + header_sz;

    size_t page_size = arena->page_size;
    if(actual_size > page_size) {
        if(arena->flags & EXT_ARENA_FIXED_PAGE_SIZE) {
#ifndef EXTLIB_NO_STD
            ext_log(EXT_ERROR,
                    "Error: requested size %zu exceeds max allocatable size in page "
                    "(%zu)",
                    requested_size, arena->page_size - header_sz);
            abort();
#else
            EXT_ASSERT(false, "requested size exceeds max allocatable size in page");
            return NULL;
#endif
        } else {
            page_size = actual_size;
        }
    }

    Ext_ArenaPage *page = ext_allocator_alloc(arena->page_allocator, page_size);
    EXT_ASSERT(page, "out of memory");
    page->next = NULL;
    page->base = 0;
    // Account for alignment of first allocation; the arena assumes every pointer
    // starts aligned to the arena's alignment.
    page->pos = EXT_ALIGN_PAD(page->data, arena->alignment);
    page->size = page_size - sizeof(Ext_ArenaPage);
    return page;
}

void *ext__arena_alloc_wrap_(Ext_Allocator *a, size_t size) {
    return ext_arena_alloc((Ext_Arena *)a, size);
}

void *ext__arena_realloc_wrap_(Ext_Allocator *a, void *ptr, size_t old_size, size_t new_size) {
    return ext_arena_realloc((Ext_Arena *)a, ptr, old_size, new_size);
}

void ext__arena_free_wrap_(Ext_Allocator *a, void *ptr, size_t size) {
    ext_arena_free((Ext_Arena *)a, ptr, size);
}

void *ext_arena_alloc(Ext_Arena *a, size_t size) {
    size = EXT_ALIGN_UP(size, a->alignment);

    if(!a->last_page) {
        EXT_ASSERT(a->first_page == NULL, "should be first allocation");
        EXT_ASSERT(((a->alignment) & (a->alignment - 1)) == 0, "alignment must be a power of 2");
        if(!a->page_allocator) a->page_allocator = ext_context->alloc;
        Ext_ArenaPage *page = ext_arena_new_page(a, size);
        a->first_page = page;
        a->last_page = page;
    }

    size_t available = a->last_page->size - a->last_page->pos;
    while(available < size) {
        Ext_ArenaPage *next_page = a->last_page->next;

        if(!next_page) {
            if(a->flags & EXT_ARENA_NO_CHAIN) {
#ifndef EXTLIB_NO_STD
                ext_log(EXT_ERROR, "Not enough space in arena: available %zu, requested %zu",
                        available, size);
                abort();
#else
                EXT_ASSERT(false, "Not enough space in arena");
                return NULL;
#endif
            }
            Ext_ArenaPage *new_page = ext_arena_new_page(a, size);
            new_page->base = a->last_page->base + a->last_page->pos;

            a->last_page->next = new_page;
            a->last_page = new_page;
            available = a->last_page->size - a->last_page->pos;
            break;
        } else {
            // Reset the page
            next_page->base = a->last_page->base + a->last_page->pos;
            next_page->pos = EXT_ALIGN_PAD(next_page->data, a->alignment);

            available = next_page->size - next_page->pos;
            a->last_page = next_page;
        }
    }

    EXT_ASSERT(available >= size, "Not enough space in arena");

    void *result = a->last_page->data + a->last_page->pos;
    EXT_ASSERT(EXT_ALIGN_PAD(result, a->alignment) == 0,
               "result not aligned to the arena's alignment");
    a->last_page->pos += size;
    if(a->flags & EXT_ARENA_ZERO_ALLOC) memset(result, 0, size);

    return result;
}

void *ext_arena_realloc(Ext_Arena *a, void *ptr, size_t old_size, size_t new_size) {
    EXT_ASSERT(EXT_ALIGN_PAD(ptr, a->alignment) == 0, "ptr not aligned to the arena's alignment");

    Ext_ArenaPage *page = a->last_page;
    EXT_ASSERT(page, "No pages in arena");

    size_t aligned_old = EXT_ALIGN_UP(old_size, a->alignment);
    if(page->data + page->pos - aligned_old == ptr) {
        // Reallocating last allocated memory, can grow/shrink page in-place
        page->pos -= aligned_old;
        void *new_ptr = ext_arena_alloc(a, new_size);
        // Can still get a different pointer in case the arena runs out of page space and needs to
        // allocate a brand new one. In this case we fallback on copying the data over.
        if(new_ptr != ptr) memcpy(new_ptr, ptr, old_size);
        return new_ptr;
    } else if(new_size > old_size) {
        void *new_ptr = ext_arena_alloc(a, new_size);
        memcpy(new_ptr, ptr, old_size);
        return new_ptr;
    } else {
        return ptr;
    }
}

void ext_arena_free(Ext_Arena *a, void *ptr, size_t size) {
    if(!ptr) return;

    EXT_ASSERT(EXT_ALIGN_PAD(ptr, a->alignment) == 0,
               "ptr is not aligned to the arena's alignment");

    Ext_ArenaPage *page = a->last_page;
    EXT_ASSERT(page, "No pages in arena");

    size = EXT_ALIGN_UP(size, a->alignment);
    if(page->data + page->pos - size == ptr) {
        // Deallocating last allocated memory, can shrink in-place
        page->pos -= size;
    } else {
        // no-op
    }
}

Ext_ArenaCheckpoint ext_arena_checkpoint(const Ext_Arena *a) {
    if(!a->last_page) {
        EXT_ASSERT(a->first_page == NULL, "arena should be empty");
        return (Ext_ArenaCheckpoint){0};
    } else {
        return (Ext_ArenaCheckpoint){
            a->last_page,
            a->last_page->pos,
        };
    }
}

void ext_arena_rewind(Ext_Arena *a, Ext_ArenaCheckpoint checkpoint) {
    if(!checkpoint.page) {
        ext_arena_reset(a);
        return;
    }
    a->last_page = checkpoint.page;
    a->last_page->pos = checkpoint.pos;
}

void ext_arena_reset(Ext_Arena *a) {
    if(!a->first_page) return;
    a->last_page = a->first_page;
    a->last_page->pos = EXT_ALIGN_PAD(a->last_page->data, a->alignment);
}

void ext_arena_destroy(Ext_Arena *a) {
    Ext_ArenaPage *page = a->first_page;
    while(page) {
        Ext_ArenaPage *next = page->next;
        ext_allocator_free(a->page_allocator, page, page->size + sizeof(Ext_ArenaPage));
        page = next;
    }
    a->first_page = NULL;
    a->last_page = NULL;
}

size_t ext_arena_get_allocated(const Ext_Arena *a) {
    if(!a->first_page) return 0;
    return a->last_page->base + a->last_page->pos;
}

char *ext_arena_strdup(Ext_Arena *a, const char *str) {
    return ext_allocator_strdup(&a->base, str);
}

void *ext_arena_memdup(Ext_Arena *a, const void *mem, size_t size) {
    return ext_allocator_memdup(&a->base, mem, size);
}

#ifndef EXTLIB_NO_STD
char *ext_arena_sprintf(Ext_Arena *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *res = ext_arena_vsprintf(a, fmt, ap);
    va_end(ap);
    return res;
}

char *ext_arena_vsprintf(Ext_Arena *a, const char *fmt, va_list ap) {
    va_list cpy;
    va_copy(cpy, ap);
    int n = vsnprintf(NULL, 0, fmt, cpy);
    va_end(cpy);

    EXT_ASSERT(n >= 0, "error in vsnprintf");
    char *res = ext_arena_alloc(a, n + 1);

    va_copy(cpy, ap);
    vsnprintf(res, n + 1, fmt, cpy);
    va_end(cpy);

    return res;
}
#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: String buffer
//
#ifndef EXTLIB_NO_STD
#include <ctype.h>
#else
static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
static inline int toupper(int c) {
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}
static inline int tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}
#endif  // EXTLIB_NO_STD

void ext_sb_replace(Ext_StringBuffer *sb, size_t start, const char *to_replace, char replacment) {
    EXT_ASSERT(start < sb->size, "start out of bounds");
    size_t to_replace_len = strlen(to_replace);
    for(size_t i = start; i < sb->size; i++) {
#ifdef EXTLIB_NO_STD
        for(size_t j = 0; j < to_replace_len; j++) {
            if(sb->items[i] == to_replace[j]) {
                sb->items[i] = replacment;
                break;
            }
        }
#else
        if(memchr(to_replace, sb->items[i], to_replace_len) != NULL) {
            sb->items[i] = replacment;
        }
#endif
    }
}

void ext_sb_to_upper(Ext_StringBuffer *sb) {
    for(size_t i = 0; i < sb->size; i++) {
        sb->items[i] = (char)toupper((unsigned char)sb->items[i]);
    }
}

void ext_sb_to_lower(Ext_StringBuffer *sb) {
    for(size_t i = 0; i < sb->size; i++) {
        sb->items[i] = (char)tolower((unsigned char)sb->items[i]);
    }
}

void ext_sb_reverse(Ext_StringBuffer *sb) {
    for(size_t i = 0, j = sb->size; i < j; i++) {
        j--;
        char tmp = sb->items[i];
        sb->items[i] = sb->items[j];
        sb->items[j] = tmp;
    }
}

char *ext_sb_to_cstr(Ext_StringBuffer *sb) {
    ext_sb_append_char(sb, '\0');
    ext_array_shrink_to_fit(sb);
    char *res = sb->items;
    *sb = (Ext_StringBuffer){0};
    return res;
}

#ifndef EXTLIB_NO_STD
int ext_sb_appendf(Ext_StringBuffer *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int res = ext_sb_appendvf(sb, fmt, ap);
    va_end(ap);
    return res;
}

int ext_sb_appendvf(Ext_StringBuffer *sb, const char *fmt, va_list ap) {
    va_list cpy;
    va_copy(cpy, ap);
    int n = vsnprintf(NULL, 0, fmt, cpy);
    va_end(cpy);

    EXT_ASSERT(n >= 0, "error in vsnprintf");
    ext_sb_reserve(sb, sb->size + n + 1);

    va_copy(cpy, ap);
    vsnprintf(sb->items + sb->size, n + 1, fmt, cpy);
    va_end(cpy);

    sb->size += n;
    return n;
}

#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: String slice
//
Ext_StringSlice ext_ss_from(const void *mem, size_t size) {
    return (Ext_StringSlice){size, mem};
}

Ext_StringSlice ext_ss_from_cstr(const char *str) {
    return ext_ss_from(str, strlen(str));
}

Ext_StringSlice ext_ss_split_once(Ext_StringSlice *ss, char delim) {
    Ext_StringSlice split = {0, ss->data};
    while(ss->size) {
        ss->size--;
        if(*ss->data++ == delim) break;
        else split.size++;
    }
    return split;
}

Ext_StringSlice ext_ss_rsplit_once(Ext_StringSlice *ss, char delim) {
    Ext_StringSlice split = {0, ss->data + ss->size};
    while(ss->size) {
        ss->size--;
        if(ss->data[ss->size] == delim) break;
        else split.size++, split.data--;
    }
    return split;
}

Ext_StringSlice ext_ss_split_once_cstr(Ext_StringSlice *ss, const char *delim) {
    size_t delim_len = strlen(delim);
    Ext_StringSlice split = {0, ss->data};
    while(ss->size >= delim_len) {
        if(memcmp(ss->data, delim, delim_len) == 0) {
            ss->data += delim_len;
            ss->size -= delim_len;
            return split;
        } else {
            ss->data++;
            ss->size--;
            split.size++;
        }
    }

    size_t max_len = ss->size < delim_len ? ss->size : delim_len;
    ss->data += max_len;
    ss->size -= max_len;
    split.size += max_len;

    return split;
}

Ext_StringSlice ext_ss_rsplit_once_cstr(Ext_StringSlice *ss, const char *delim) {
    size_t delim_len = strlen(delim);
    Ext_StringSlice split = {0, ss->data + ss->size};
    while(ss->size >= delim_len) {
        if(memcmp(ss->data + ss->size - delim_len, delim, delim_len) == 0) {
            ss->size -= delim_len;
            return split;
        } else {
            ss->size--;
            split.size++;
            split.data--;
        }
    }

    size_t max_len = ss->size < delim_len ? ss->size : delim_len;
    ss->size -= max_len;
    split.size += max_len;
    split.data -= max_len;

    return split;
}

ptrdiff_t ext_ss_find_char(Ext_StringSlice ss, char c, size_t offset) {
    for(size_t i = offset; i < ss.size; i++) {
        if(ss.data[i] == c) return i;
    }
    return -1;
}

ptrdiff_t ext_ss_rfind_char(Ext_StringSlice ss, char c, size_t offset) {
    size_t start = offset < ss.size ? offset : ss.size;
    for(size_t i = start; i > 0; i--) {
        if(ss.data[i - 1] == c) return (ptrdiff_t)(i - 1);
    }
    return -1;
}

ptrdiff_t ext_ss_find(Ext_StringSlice ss, Ext_StringSlice needle, size_t offset) {
    if(needle.size == 0) return offset <= ss.size ? (ptrdiff_t)offset : -1;
    if(needle.size > ss.size) return -1;
    for(size_t i = offset; i + needle.size <= ss.size; i++) {
        if(memcmp(ss.data + i, needle.data, needle.size) == 0) return i;
    }
    return -1;
}

ptrdiff_t ext_ss_rfind(Ext_StringSlice ss, Ext_StringSlice needle, size_t offset) {
    if(needle.size == 0) {
        size_t pos = offset < ss.size ? offset : ss.size;
        return pos;
    }
    if(needle.size > ss.size) return -1;
    size_t last = ss.size - needle.size;
    size_t start = offset < last ? offset : last;
    for(size_t i = start + 1; i > 0; i--) {
        if(memcmp(ss.data + i - 1, needle.data, needle.size) == 0) return i - 1;
    }
    return -1;
}

ptrdiff_t ext_ss_find_cstr(Ext_StringSlice ss, const char *needle, size_t offset) {
    return ext_ss_find(ss, ext_ss_from_cstr(needle), offset);
}

ptrdiff_t ext_ss_rfind_cstr(Ext_StringSlice ss, const char *needle, size_t offset) {
    return ext_ss_rfind(ss, ext_ss_from_cstr(needle), offset);
}

static bool any_match(char c, const char *set, size_t set_len) {
#ifdef EXTLIB_NO_STD
    for(size_t i = 0; i < set_len; i++) {
        if(c == set[i]) return true;
    }
    return false;
#else
    return memchr(set, c, set_len) != NULL;
#endif  // EXTLIB_NO_STD
}

Ext_StringSlice ext_ss_split_once_any(Ext_StringSlice *ss, const char *set) {
    size_t set_len = strlen(set);
    Ext_StringSlice split = {0, ss->data};
    while(ss->size) {
        ss->size--;
        if(any_match(*ss->data++, set, set_len)) break;
        else split.size++;
    }
    return split;
}

Ext_StringSlice ext_ss_rsplit_once_any(Ext_StringSlice *ss, const char *set) {
    size_t set_len = strlen(set);
    Ext_StringSlice split = {0, ss->data + ss->size};
    while(ss->size) {
        ss->size--;
        if(any_match(ss->data[ss->size], set, set_len)) break;
        else split.size++, split.data--;
    }
    return split;
}

Ext_StringSlice ext_ss_split_once_ws(Ext_StringSlice *ss) {
    Ext_StringSlice split = {0, ss->data};
    while(ss->size) {
        ss->size--;
        if(isspace(*ss->data++)) break;
        else split.size++;
    }
    while(ss->size && isspace(*ss->data)) {
        ss->data++;
        ss->size--;
    }
    return split;
}

Ext_StringSlice ext_ss_rsplit_once_ws(Ext_StringSlice *ss) {
    Ext_StringSlice split = {0, ss->data + ss->size};
    while(ss->size) {
        if(isspace(ss->data[--ss->size])) break;
        else split.size++, split.data--;
    }
    while(ss->size && isspace(ss->data[ss->size - 1])) {
        ss->size--;
    }
    return split;
}

Ext_StringSlice ext_ss_trim_start(Ext_StringSlice ss) {
    while(ss.size && isspace(*ss.data)) ss.data++, ss.size--;
    return ss;
}

Ext_StringSlice ext_ss_trim_end(Ext_StringSlice ss) {
    while(ss.size && isspace(ss.data[ss.size - 1])) ss.size--;
    return ss;
}

Ext_StringSlice ext_ss_trim(Ext_StringSlice ss) {
    return ext_ss_trim_end(ext_ss_trim_start(ss));
}

Ext_StringSlice ext_ss_cut(Ext_StringSlice ss, size_t n) {
    if(n > ss.size) n = ss.size;
    ss.data += n;
    ss.size -= n;
    return ss;
}

Ext_StringSlice ext_ss_trunc(Ext_StringSlice ss, size_t n) {
    if(n > ss.size) n = ss.size;
    ss.size -= ss.size - n;
    return ss;
}

Ext_StringSlice ext_ss_substr(Ext_StringSlice ss, size_t start, size_t len) {
    return ext_ss_trunc(ext_ss_cut(ss, start), len);
}

bool ext_ss_starts_with(Ext_StringSlice ss, Ext_StringSlice prefix) {
    return prefix.size <= ss.size && memcmp(ss.data, prefix.data, prefix.size) == 0;
}

bool ext_ss_ends_with(Ext_StringSlice ss, Ext_StringSlice suffix) {
    return suffix.size <= ss.size &&
           memcmp(&ss.data[ss.size - suffix.size], suffix.data, suffix.size) == 0;
}

Ext_StringSlice ext_ss_strip_prefix(Ext_StringSlice ss, Ext_StringSlice prefix) {
    if(ext_ss_starts_with(ss, prefix)) return ext_ss_cut(ss, prefix.size);
    return ss;
}

Ext_StringSlice ext_ss_strip_suffix(Ext_StringSlice ss, Ext_StringSlice suffix) {
    if(ext_ss_ends_with(ss, suffix)) return ext_ss_trunc(ss, ss.size - suffix.size);
    return ss;
}

Ext_StringSlice ext_ss_strip_prefix_cstr(Ext_StringSlice ss, const char *prefix) {
    return ext_ss_strip_prefix(ss, ext_ss_from_cstr(prefix));
}

Ext_StringSlice ext_ss_strip_suffix_cstr(Ext_StringSlice ss, const char *suffix) {
    return ext_ss_strip_suffix(ss, ext_ss_from_cstr(suffix));
}

int ext_ss_cmp(Ext_StringSlice s1, Ext_StringSlice s2) {
    if(s1.size < s2.size) return -1;
    else if(s1.size > s2.size) return 1;
    else return memcmp(s1.data, s2.data, s1.size);
}

bool ext_ss_eq(Ext_StringSlice s1, Ext_StringSlice s2) {
    return s1.size == s2.size && memcmp(s1.data, s2.data, s1.size) == 0;
}

bool ext_ss_eq_ignore_case(Ext_StringSlice a, Ext_StringSlice b) {
    if(a.size != b.size) return false;
    for(size_t i = 0; i < a.size; i++) {
        if(tolower((unsigned char)a.data[i]) != tolower((unsigned char)b.data[i])) return false;
    }
    return true;
}

int ext_ss_cmp_ignore_case(Ext_StringSlice a, Ext_StringSlice b) {
    size_t min_sz = a.size < b.size ? a.size : b.size;
    for(size_t i = 0; i < min_sz; i++) {
        int ca = tolower((unsigned char)a.data[i]);
        int cb = tolower((unsigned char)b.data[i]);
        if(ca != cb) return ca - cb;
    }
    if(a.size < b.size) return -1;
    if(a.size > b.size) return 1;
    return 0;
}

bool ext_ss_starts_with_ignore_case(Ext_StringSlice ss, Ext_StringSlice prefix) {
    if(prefix.size > ss.size) return false;
    for(size_t i = 0; i < prefix.size; i++) {
        if(tolower((unsigned char)ss.data[i]) != tolower((unsigned char)prefix.data[i]))
            return false;
    }
    return true;
}

bool ext_ss_ends_with_ignore_case(Ext_StringSlice ss, Ext_StringSlice suffix) {
    if(suffix.size > ss.size) return false;
    size_t offset = ss.size - suffix.size;
    for(size_t i = 0; i < suffix.size; i++) {
        if(tolower((unsigned char)ss.data[offset + i]) != tolower((unsigned char)suffix.data[i]))
            return false;
    }
    return true;
}

bool ext_ss_starts_with_ignore_case_cstr(Ext_StringSlice ss, const char *prefix) {
    return ext_ss_starts_with_ignore_case(ss, ext_ss_from_cstr(prefix));
}

bool ext_ss_ends_with_ignore_case_cstr(Ext_StringSlice ss, const char *suffix) {
    return ext_ss_ends_with_ignore_case(ss, ext_ss_from_cstr(suffix));
}

char *ext_ss_to_cstr(Ext_StringSlice ss) {
    return ext_ss_to_cstr_alloc(ss, ext_context->alloc);
}

char *ext_ss_to_cstr_temp(Ext_StringSlice ss) {
    return ext_ss_to_cstr_alloc(ss, &ext_temp_allocator.base);
}

char *ext_ss_to_cstr_alloc(Ext_StringSlice ss, Ext_Allocator *a) {
    char *res = a->alloc(a, ss.size + 1);
    memcpy(res, ss.data, ss.size);
    res[ss.size] = '\0';
    return res;
}

static bool ext__is_path_sep(char c) {
#ifdef EXT_WINDOWS
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

#ifdef EXT_WINDOWS
static bool ext__is_drive_letter(Ext_StringSlice path) {
    if(path.size < 2) return false;
    char c = path.data[0];
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && path.data[1] == ':';
}

static bool ext__is_unc_path(Ext_StringSlice path) {
    return path.size >= 2 && ext__is_path_sep(path.data[0]) && ext__is_path_sep(path.data[1]);
}

// Find the UNC root (e.g., server and share from path)
// Returns the length of the root, or 0 if not a valid UNC path
static size_t ext__unc_root_length(Ext_StringSlice path) {
    if(!ext__is_unc_path(path)) return 0;
    size_t pos = 2;  // Skip initial separators

    // Special cases: extended-length and device paths
    if(pos < path.size && path.data[pos] == '?') {
        pos++;  // Skip '?'
        if(pos < path.size && ext__is_path_sep(path.data[pos])) {
            pos++;  // Skip separator
            // Check for drive letter format
            if(pos + 1 < path.size && path.data[pos + 1] == ':') {
                return pos + 2;  // Include drive letter and colon
            }
            // Check for UNC format
            if(pos + 3 < path.size && (path.data[pos] == 'U' || path.data[pos] == 'u') &&
               (path.data[pos + 1] == 'N' || path.data[pos + 1] == 'n') &&
               (path.data[pos + 2] == 'C' || path.data[pos + 2] == 'c') &&
               ext__is_path_sep(path.data[pos + 3])) {
                pos += 4;  // Skip "UNC" and separator
                // Fall through to find server and share
            }
        }
    } else if(pos < path.size && path.data[pos] == '.') {
        pos++;  // Skip '.'
        if(pos < path.size && ext__is_path_sep(path.data[pos])) {
            pos++;  // Skip separator
            // Device format - find next separator or end
            while(pos < path.size && !ext__is_path_sep(path.data[pos])) {
                pos++;
            }
            return pos;
        }
    }

    // Standard UNC: find server name (up to next separator)
    while(pos < path.size && !ext__is_path_sep(path.data[pos])) {
        pos++;
    }
    if(pos >= path.size) return 0;  // Invalid: no share name

    pos++;  // Skip separator after server

    // Find share name (up to next separator or end)
    while(pos < path.size && !ext__is_path_sep(path.data[pos])) {
        pos++;
    }

    return pos;
}
#endif

Ext_StringSlice ext_ss_basename(Ext_StringSlice path) {
    // Strip trailing separators
    while(path.size > 0 && ext__is_path_sep(path.data[path.size - 1])) {
        path.size--;
    }
    for(size_t i = path.size; i > 0; i--) {
        if(ext__is_path_sep(path.data[i - 1])) {
            return ext_ss_cut(path, i);
        }
    }
    return path;
}

Ext_StringSlice ext_ss_dirname(Ext_StringSlice path) {
#ifdef EXT_WINDOWS
    size_t unc_root = ext__unc_root_length(path);
    if(unc_root > 0) {
        // Strip trailing separators after UNC root
        size_t end = path.size;
        while(end > unc_root && ext__is_path_sep(path.data[end - 1])) {
            end--;
        }
        for(size_t i = end; i > unc_root; i--) {
            if(ext__is_path_sep(path.data[i - 1])) {
                size_t dir_end = i - 1;
                while(dir_end > unc_root && ext__is_path_sep(path.data[dir_end - 1])) {
                    dir_end--;
                }
                return ext_ss_trunc(path, dir_end);
            }
        }

        // No separator after UNC root - return the root itself
        return ext_ss_trunc(path, unc_root);
    }
#endif

    // Strip trailing separators (but keep at least one char for root paths)
    size_t end = path.size;
    while(end > 1 && ext__is_path_sep(path.data[end - 1])) {
        end--;
    }

    // Find last separator
    for(size_t i = end; i > 0; i--) {
        if(ext__is_path_sep(path.data[i - 1])) {
            size_t dir_end = i - 1;
            while(dir_end > 0 && ext__is_path_sep(path.data[dir_end - 1])) {
                dir_end--;
            }

            // Handle root paths
            if(dir_end == 0) {
                dir_end = 1;  // Unix root "/"
            }
#ifdef EXT_WINDOWS
            // Windows: if we're at position 1 and have a drive letter, include the colon
            else if(dir_end == 1 && path.size >= 2 && path.data[1] == ':') {
                dir_end = 2;  // Windows drive "C:"
            }
#endif
            return ext_ss_trunc(path, dir_end);
        }
    }

#ifdef EXT_WINDOWS
    // If path is just a drive letter (e.g., "C:"), return it as-is
    if(ext__is_drive_letter(path)) {
        return ext_ss_trunc(path, 2);
    }
#endif

    // No separator found
    return (Ext_StringSlice){0, path.data};
}

Ext_StringSlice ext_ss_extension(Ext_StringSlice path) {
    Ext_StringSlice base = ext_ss_basename(path);
    for(size_t i = base.size; i > 0; i--) {
        if(base.data[i - 1] == '.') {
            // Dotfile (e.g. ".gitignore") with no other dot — no extension
            if(i - 1 == 0) return (Ext_StringSlice){0, base.data + base.size};
            return ext_ss_cut(base, i - 1);
        }
    }
    return (Ext_StringSlice){0, base.data + base.size};
}

void ext_sb_append_path(Ext_StringBuffer *sb, Ext_StringSlice component) {
    if(sb->size > 0 && !ext__is_path_sep(sb->items[sb->size - 1])) {
#ifdef EXT_WINDOWS
        // Use the same separator style as the existing path
        char sep = '/';
        for(size_t i = 0; i < sb->size; i++) {
            if(sb->items[i] == '\\') {
                sep = '\\';
                break;
            }
        }
        ext_sb_append_char(sb, sep);
#else
        ext_sb_append_char(sb, '/');
#endif
    }
    ext_sb_append(sb, component.data, component.size);
}

void ext_sb_append_path_cstr(Ext_StringBuffer *sb, const char *component) {
    ext_sb_append_path(sb, ext_ss_from_cstr(component));
}

// -----------------------------------------------------------------------------
// SECTION: IO
//
#ifndef EXTLIB_NO_STD

#if defined(EXT_POSIX) && !(defined(_POSIX_C_SOURCE) && defined(__USE_POSIX2))
FILE *popen(const char *command, const char *type);
int pclose(FILE *stream);
#elif defined(EXT_WINDOWS)
#define popen  _popen
#define pclose _pclose
#endif  // defined(EXT_POSIX) && !(defined(_POSIX_C_SOURCE) && defined(__USE_POSIX2))

#ifdef EXT_WINDOWS
#define _WINUSER_
#define _WINGDI_
#define _IMM_
#define _WINCON_
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>

#define getcwd _getcwd
#define chdir  _chdir

static char *win32_strerror(DWORD error) {
    static char buffer[4096 * sizeof(TCHAR)];

    DWORD size;
    if(!(size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                               error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer,
                               sizeof(buffer), NULL))) {
        ext_log(EXT_ERROR, "FormatMessage failed with %lu", GetLastError());
        return NULL;
    }

    Ext_StringSlice ss = ext_ss_from(buffer, size * sizeof(TCHAR));
    ss = ext_ss_trim_end(ss);
    buffer[ss.size] = '\0';
    return buffer;
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
char *realpath(const char *restrict path, char *restrict resolved_path);
#endif  // EXT_WINDOWS

void ext_free_paths(Ext_Paths *paths) {
    if(!paths->allocator) return;
    ext_array_foreach(char *, it, paths) {
        paths->allocator->free(paths->allocator, *it, strlen(*it) + 1);
    }
    ext_array_free(paths);
}

bool ext_read_file(const char *path, Ext_StringBuffer *sb) {
    bool res = true;
    FILE *f = fopen(path, "rb");
    if(!f) ext_return_exit(false);
    if(fseek(f, 0, SEEK_END) < 0) ext_return_exit(false);

    long long size;
#ifdef EXT_WINDOWS
    size = _ftelli64(f);
#else
    size = ftell(f);
#endif  // EXT_WINDOWS
    if(size < 0) ext_return_exit(false);
    if(fseek(f, 0, SEEK_SET) < 0) ext_return_exit(false);

    ext_sb_reserve_exact(sb, sb->size + size);
    size_t nread = fread(sb->items + sb->size, 1, size, f);
    if(nread < (size_t)size) ext_return_exit(false);
    if(ferror(f)) ext_return_exit(false);
    sb->size = size;

exit:;
    int saved_errno = errno;
    if(!res) ext_log(EXT_ERROR, "couldn't read file %s: %s", path, strerror(errno));
    if(f) fclose(f);
    errno = saved_errno;
    return res;
}

bool ext_write_file(const char *path, const void *mem, size_t size) {
    bool res = true;
    FILE *f = fopen(path, "wb");
    if(!f) ext_return_exit(false);

    const char *data = mem;
    while(size > 0) {
        size_t written = fwrite(data, 1, size, f);
        if(ferror(f)) ext_return_exit(false);
        size -= written;
        data += written;
    }

exit:;
    int saved_errno = errno;
    if(!res) ext_log(EXT_ERROR, "couldn't write file %s: %s", path, strerror(errno));
    if(f) fclose(f);
    errno = saved_errno;
    return res;
}

int ext_read_line(FILE *f, Ext_StringBuffer *sb) {
    ext_sb_reserve(sb, 256);

    char *res;
    while((res = fgets(sb->items + sb->size, sb->capacity - sb->size, f)) != NULL) {
        sb->size += strlen(sb->items + sb->size);
        if(sb->items[sb->size - 1] == '\n') break;
        ext_sb_reserve(sb, sb->size * 2);
    }

    if(ferror(f)) {
        int saveErrno = errno;
        ext_log(EXT_ERROR, "couldn't read line: %s", strerror(errno));
        errno = saveErrno;
        return -1;
    }

    if(feof(f)) {
        return 0;
    }

    return 1;
}

bool ext_read_dir(const char *path, Ext_Paths *paths) {
    bool res = true;
    Ext_Allocator *a = paths->allocator;
    if(!a) a = ext_context->alloc;

#ifdef EXT_WINDOWS
    void *checkpoint = ext_temp_checkpoint();

    char *dir = ext_temp_sprintf("%s\\*", path);
    WIN32_FIND_DATA find_data;
    HANDLE dir_handle = FindFirstFile(dir, &find_data);
    if(dir_handle == INVALID_HANDLE_VALUE) ext_return_exit(false);

    do {
        if(strcmp(".", find_data.cFileName) != 0 && strcmp("..", find_data.cFileName) != 0) {
            ext_array_push(paths, ext_allocator_strdup(a, find_data.cFileName));
        }
    } while(FindNextFile(dir_handle, &find_data) != 0);

    if(GetLastError() != ERROR_NO_MORE_FILES) ext_return_exit(false);

exit:
    if(!res) {
        ext_log(EXT_ERROR, "couldn't read directory '%s': %s", path,
                win32_strerror(GetLastError()));
    }
    if(dir_handle != INVALID_HANDLE_VALUE) FindClose(dir_handle);
    ext_temp_rewind(checkpoint);
    return res;
#else
    DIR *dir = opendir(path);
    if(!dir) ext_return_exit(false);

    for(;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if(!entry) ext_return_exit(errno ? false : true);
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        ext_array_push(paths, ext_allocator_strdup(a, entry->d_name));
    }

exit:
    if(!res) ext_log(EXT_ERROR, "couldn't read directory '%s': %s", path, strerror(errno));
    if(dir) closedir(dir);
    return res;
#endif  // EXT_WINDOWS
}

bool ext_create_dir(const char *path) {
    int res;
#ifdef EXT_WINDOWS
    res = mkdir(path);
#else
    res = mkdir(path, 0755);
#endif  // EXT_WINDOWS
    if(res < 0) {
        if(errno == EEXIST) {
            ext_log(EXT_INFO, "directory `%s` already exists", path);
            return true;
        }
        ext_log(EXT_ERROR, "couldn't create directory '%s': %s", path, strerror(errno));
        return false;
    }
    ext_log(EXT_INFO, "created directory '%s'", path);
    return true;
}

static bool ext__delete_dir_(const char *path) {
#ifdef EXT_WINDOWS
    static const char *path_seps = "\\/";
#else
    static const char *path_seps = "/";
#endif  // EXT_WINDOWS

    bool res = true;
    Ext_Paths paths = {0};
    if(!ext_read_dir(path, &paths)) ext_return_exit(false);

    ext_array_foreach(char *, it, &paths) {
        void *checkpoint = ext_temp_checkpoint();

        char *entry = *it;
        const char *abs;
        if(strchr(path_seps, path[strlen(path) - 1]) != NULL) {
            abs = ext_temp_sprintf("%s%s", path, entry);
        } else {
            abs = ext_temp_sprintf("%s%c%s", path, path_seps[0], entry);
        }

        Ext_FileType type = ext_get_file_type(abs);
        if(type == EXT_FILE_ERR) ext_return_exit(false);

        if(type == EXT_FILE_DIR) {
            if(!ext__delete_dir_(abs)) ext_return_exit(false);
        } else {
            if(!ext_delete_file(abs)) ext_return_exit(false);
        }

        ext_temp_rewind(checkpoint);
    }

    if(!ext_delete_file(path)) ext_return_exit(false);

exit:
    ext_free_paths(&paths);
    return res;
}

bool ext_delete_dir_recursively(const char *path) {
    Ext_FileType type = ext_get_file_type(path);
    if(type != EXT_FILE_DIR) {
        ext_log(EXT_ERROR, "coldn't delete directory '%s': Not a directory", path);
        return false;
    }

    Ext_Context ctx = *ext_context;
    ctx.log_level = EXT_ERROR;

    ext_push_context(&ctx);
    bool res = ext__delete_dir_(path);
    ext_pop_context();

    if(res) ext_log(EXT_INFO, "deleted directory '%s'", path);
    else ext_log(EXT_ERROR, "couldn't recursively delete directory '%s'", path);
    return res;
}

Ext_FileType ext_get_file_type(const char *path) {
#ifdef EXT_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    if(attr == INVALID_FILE_ATTRIBUTES) {
        ext_log(EXT_ERROR, "Couldn't stat '%s': %s", path, win32_strerror(GetLastError()));
        return EXT_FILE_ERR;
    }
    if(attr & FILE_ATTRIBUTE_DIRECTORY) return EXT_FILE_DIR;
    return EXT_FILE_REGULAR;
#else
    struct stat statbuf;
    if(stat(path, &statbuf) < 0) {
        ext_log(EXT_ERROR, "Couldn't stat '%s': %s", path, strerror(errno));
        return EXT_FILE_ERR;
    }
    if(S_ISREG(statbuf.st_mode)) return EXT_FILE_REGULAR;
    if(S_ISDIR(statbuf.st_mode)) return EXT_FILE_DIR;
    if(S_ISLNK(statbuf.st_mode)) return EXT_FILE_SYMLINK;
    return EXT_FILE_OTHER;
#endif  // EXT_WINDOWS
}

bool ext_rename_file(const char *old_path, const char *new_path) {
#ifdef EXT_WINDOWS
    if(!MoveFileEx(old_path, new_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        ext_log(EXT_ERROR, "couldn't rename '%s' -> '%s': %s", old_path, new_path,
                win32_strerror(GetLastError()));
        return false;
    }
#else
    if(rename(old_path, new_path) < 0) {
        ext_log(EXT_ERROR, "couldn't rename '%s' -> '%s': %s", old_path, new_path, strerror(errno));
        return false;
    }
#endif  // EXT_WINDOWS
    ext_log(EXT_INFO, "renamed '%s' -> '%s'", old_path, new_path);
    return true;
}

bool ext_delete_file(const char *path) {
#ifdef EXT_WINDOWS
    Ext_FileType type = ext_get_file_type(path);
    if(type == EXT_FILE_ERR) return false;

    BOOL res;
    if(type == EXT_FILE_DIR) res = RemoveDirectory(path);
    else res = DeleteFile(path);

    if(!res) {
        ext_log(EXT_ERROR, "couldn't delete '%s': %s", path, win32_strerror(GetLastError()));
        return false;
    }
#else
    if(remove(path) < 0) {
        ext_log(EXT_ERROR, "couldn't delete '%s': %s", path, strerror(errno));
        return false;
    }
#endif  // EXT_WINDOWS
    ext_log(EXT_INFO, "deleted '%s'", path);
    return true;
}

char *ext_get_cwd(void) {
    return ext_get_cwd_alloc(ext_context->alloc);
}

char *ext_get_cwd_alloc(Ext_Allocator *a) {
    Ext_StringBuffer sb = {0};
    sb.allocator = a;
    ext_sb_reserve(&sb, 128);

    while(!getcwd(sb.items, sb.capacity)) {
        if(errno != ERANGE) {
            int saveErrno = errno;
            ext_log(EXT_ERROR, "couldn't get cwd: %s", strerror(errno));
            ext_sb_free(&sb);
            errno = saveErrno;
            return NULL;
        }
        ext_sb_reserve(&sb, sb.capacity * 2);
    }

    sb.size = strlen(sb.items);
    return ext_sb_to_cstr(&sb);
}

char *ext_get_cwd_temp(void) {
    return ext_get_cwd_alloc(&ext_temp_allocator.base);
}

bool ext_set_cwd(const char *cwd) {
    if(chdir(cwd) < 0) {
        int saveErrno = errno;
        ext_log(EXT_ERROR, "couldn't change cwd to '%s': %s", cwd, strerror(errno));
        errno = saveErrno;
        return false;
    }
    return true;
}

char *ext_get_abs_path_alloc(const char *path, Ext_Allocator *a) {
#ifdef EXT_WINDOWS
    // To align the behaviour with `realpath`
    if(ext_get_file_type(path) == EXT_FILE_ERR) return NULL;
    DWORD size = GetFullPathNameA(path, 0, NULL, NULL);
    if(size == 0) {
        ext_log(EXT_ERROR, "couldn't convert '%s' into an absolute path: %s", path,
                win32_strerror(GetLastError()));
        return NULL;
    }
    char *abs = a->alloc(a, size * sizeof(TCHAR));
    DWORD res = GetFullPathNameA(path, size * sizeof(TCHAR), abs, NULL);
    if(size == 0) {
        a->free(a, abs, size * sizeof(TCHAR));
        ext_log(EXT_ERROR, "couldn't convert '%s' into an absolute path: %s", path,
                win32_strerror(GetLastError()));
        return NULL;
    }
    EXT_ASSERT(res + 1 == size, "error getting full path name");
    return abs;
#else
    char *abs = realpath(path, NULL);
    if(!abs) {
        int saveErrno = errno;
        ext_log(EXT_ERROR, "couldn't convert '%s' into an absolute path: %s", path,
                strerror(errno));
        errno = saveErrno;
        return NULL;
    }
    // This is stupid, but since we are not guaranteed to have PATH_MAX on all posix systems,
    // this is the ony safe way to do it
    char *abs_cpy = a->alloc(a, strlen(abs) + 1);
    strcpy(abs_cpy, abs);
    free(abs);
    return abs_cpy;
#endif
}

char *ext_get_abs_path_temp(const char *path) {
    return ext_get_abs_path_alloc(path, &ext_temp_allocator.base);
}

char *ext_get_abs_path(const char *path) {
    return ext_get_abs_path_alloc(path, ext_context->alloc);
}

int ext_cmd(const char *cmd) {
    EXT_ASSERT(cmd, "cmd is NULL");
    ext_log(EXT_INFO, "[CMD] %s", cmd);
    int res = system(cmd);
    if(res < 0) {
        ext_log(EXT_ERROR, "couldn't exec cmd '%s': %s", cmd, strerror(errno));
        return res;
    }
    return res;
}

int ext_cmd_read(const char *cmd, Ext_StringBuffer *sb) {
    EXT_ASSERT(cmd, "cmd is NULL");
    ext_log(EXT_INFO, "[CMD] %s", cmd);

    int res = 0;
#ifdef EXT_WINDOWS
    const char *mode = "rb";
#else
    const char *mode = "r";
#endif  // EXT_WINDOWS
    FILE *p = popen(cmd, mode);
    if(!p) ext_return_exit(-1);

    const size_t chunk_size = 512;
    for(;;) {
        ext_sb_reserve(sb, sb->size + chunk_size);
        size_t read = fread(sb->items + sb->size, 1, sb->capacity - sb->size, p);
        sb->size += read;
        if(ferror(p)) ext_return_exit(-1);
        if(feof(p)) break;
    }

exit:;
    int saved_errno = errno;
    if(res) ext_log(EXT_ERROR, "couldn't exec cmd '%s' for read: %s", cmd, strerror(errno));
    if(p && (res = pclose(p))) ext_log(EXT_ERROR, "command returned exit code %d", res);
    errno = saved_errno;
    return res;
}

int ext_cmd_write(const char *cmd, const void *mem, size_t size) {
    EXT_ASSERT(cmd, "cmd is NULL");
    ext_log(EXT_INFO, "[CMD] %s", cmd);

    int res = 0;
#ifdef EXT_WINDOWS
    const char *mode = "wb";
#else
    const char *mode = "w";
#endif  // EXT_WINDOWS
    FILE *p = popen(cmd, mode);
    if(!p) ext_return_exit(-1);

    const char *data = mem;
    while(size > 0) {
        size_t written = fwrite(data, 1, size, p);
        if(ferror(p)) ext_return_exit(-1);
        size -= written;
        data += written;
    }

exit:;
    int saved_errno = errno;
    if(res) ext_log(EXT_ERROR, "couldn't exec cmd '%s' for write: %s", cmd, strerror(errno));
    if(p && (res = pclose(p))) ext_log(EXT_ERROR, "command returned exit code %d", res);
    errno = saved_errno;
    return res;
}
#endif  // EXTLIB_NO_STD

// -----------------------------------------------------------------------------
// SECTION: Hashmap
//
void ext_hmap_grow_(void **entries, size_t entries_sz, size_t **hashes, size_t *cap,
                    Ext_Allocator **a) {
    size_t newcap = *cap ? (*cap + 1) * 2 : EXT_HMAP_INIT_CAPACITY;
    size_t newsz = (newcap + 1) * entries_sz;
    size_t pad = EXT_ALIGN_PAD(newsz, sizeof(size_t));
    size_t totalsz = newsz + pad + sizeof(size_t) * (newcap + 1);
    if(!*a) *a = ext_context->alloc;
    void *newentries = ext_allocator_alloc(*a, totalsz);
    size_t *newhashes = (size_t *)((char *)newentries + newsz + pad);
    EXT_ASSERT(((uintptr_t)newhashes & (sizeof(size_t) - 1)) == 0,
               "newhashes allocation is not aligned");
    memset(newhashes, 0, sizeof(size_t) * (newcap + 1));
    if(*cap > 0) {
        for(size_t i = 1; i <= *cap + 1; i++) {
            size_t hash = (*hashes)[i];
            if(EXT_HMAP_IS_VALID(hash)) {
                size_t newidx = (hash & (newcap - 1));
                while(!EXT_HMAP_IS_EMPTY(newhashes[newidx + 1])) {
                    newidx = ((newidx + 1) & (newcap - 1));
                }
                memcpy((char *)newentries + (newidx + 1) * entries_sz,
                       (char *)(*entries) + i * entries_sz, entries_sz);
                newhashes[newidx + 1] = hash;
            }
        }
    }
    if(*entries) {
        size_t sz = (*cap + 2) * entries_sz;
        size_t pad = EXT_ALIGN_PAD(sz, sizeof(size_t));
        size_t totalsz = sz + pad + sizeof(size_t) * (*cap + 2);
        ext_allocator_free(*a, *entries, totalsz);
    }
    *entries = newentries;
    *hashes = newhashes;
    *cap = newcap - 1;
}
#endif  // EXTLIB_IMPL

void *ext__arena_alloc_wrap_(Ext_Allocator *a, size_t size);
void *ext__arena_realloc_wrap_(Ext_Allocator *a, void *ptr, size_t old_size, size_t new_size);
void ext__arena_free_wrap_(Ext_Allocator *a, void *ptr, size_t size);

#if ((defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || defined(__GNUC__)) && \
    !defined(EXTLIB_NO_STD)
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif  // __GNUC__
static inline char ext_dbg_char(const char *name, const char *file, int line, char val) {
    fprintf(stderr, "%s:%d: %s = '%c'\n", file, line, name, val);
    return val;
}
static inline signed char ext_dbg_signed_char(const char *name, const char *file, int line,
                                              signed char val) {
    fprintf(stderr, "%s:%d: %s = %hhd\n", file, line, name, val);
    return val;
}
static inline unsigned char ext_dbg_unsigned_char(const char *name, const char *file, int line,
                                                  unsigned char val) {
    fprintf(stderr, "%s:%d: %s = %hhu\n", file, line, name, val);
    return val;
}
static inline short ext_dbg_short(const char *name, const char *file, int line, short val) {
    fprintf(stderr, "%s:%d: %s = %hd\n", file, line, name, val);
    return val;
}
static inline unsigned short ext_dbg_unsigned_short(const char *name, const char *file, int line,
                                                    unsigned short val) {
    fprintf(stderr, "%s:%d: %s = %hu\n", file, line, name, val);
    return val;
}
static inline int ext_dbg_int(const char *name, const char *file, int line, int val) {
    fprintf(stderr, "%s:%d: %s = %d\n", file, line, name, val);
    return val;
}
static inline unsigned int ext_dbg_unsigned_int(const char *name, const char *file, int line,
                                                unsigned int val) {
    fprintf(stderr, "%s:%d: %s = %u\n", file, line, name, val);
    return val;
}
static inline long ext_dbg_long(const char *name, const char *file, int line, long val) {
    fprintf(stderr, "%s:%d: %s = %ld\n", file, line, name, val);
    return val;
}
static inline unsigned long ext_dbg_unsigned_long(const char *name, const char *file, int line,
                                                  unsigned long val) {
    fprintf(stderr, "%s:%d: %s = %lu\n", file, line, name, val);
    return val;
}
static inline long long ext_dbg_long_long(const char *name, const char *file, int line,
                                          long long val) {
    fprintf(stderr, "%s:%d: %s = %lld\n", file, line, name, val);
    return val;
}
static inline unsigned long long ext_dbg_unsigned_long_long(const char *name, const char *file,
                                                            int line, unsigned long long val) {
    fprintf(stderr, "%s:%d: %s = %llu\n", file, line, name, val);
    return val;
}
static inline float ext_dbg_float(const char *name, const char *file, int line, float val) {
    fprintf(stderr, "%s:%d: %s = %f\n", file, line, name, val);
    return val;
}
static inline double ext_dbg_double(const char *name, const char *file, int line, double val) {
    fprintf(stderr, "%s:%d: %s = %f\n", file, line, name, val);
    return val;
}
static inline long double ext_dbg_long_double(const char *name, const char *file, int line,
                                              long double val) {
    fprintf(stderr, "%s:%d: %s = %Lf\n", file, line, name, val);
    return val;
}
static inline const char *ext_dbg_cstr(const char *name, const char *file, int line,
                                       const char *val) {
    fprintf(stderr, "%s:%d: %s = \"%s\"\n", file, line, name, val);
    return val;
}
static inline char *ext_dbg_str(const char *name, const char *file, int line, char *val) {
    fprintf(stderr, "%s:%d: %s = \"%s\"\n", file, line, name, val);
    return val;
}
static inline void *ext_dbg_voidptr(const char *name, const char *file, int line, void *val) {
    fprintf(stderr, "%s:%d: %s = %p\n", file, line, name, val);
    return val;
}
static inline const void *ext_dbg_cvoidptr(const char *name, const char *file, int line,
                                           const void *val) {
    fprintf(stderr, "%s:%d: %s = %p\n", file, line, name, val);
    return val;
}

static inline Ext_StringSlice ext_dbg_ss(const char *name, const char *file, int line,
                                         Ext_StringSlice val) {
    fprintf(stderr, "%s:%d: %s = (StringSlice){%zu, \"" Ext_SS_Fmt "\"}\n", file, line, name,
            val.size, Ext_SS_Arg(val));
    return val;
}
static inline Ext_StringBuffer ext_dbg_sb(const char *name, const char *file, int line,
                                          Ext_StringBuffer val) {
    fprintf(stderr,
            "%s:%d: %s = (StringBuffer){.size = %zu, .capacity = %zu, .items = \"" Ext_SB_Fmt
            "\"}\n",
            file, line, name, val.size, val.capacity, Ext_SB_Arg(val));
    return val;
}
static inline Ext_StringBuffer *ext_dbg_ptr_sb(const char *name, const char *file, int line,
                                               Ext_StringBuffer *val) {
    fprintf(stderr,
            "%s:%d: %s = (StringBuffer*){.size = %zu, .capacity = %zu, .items = \"" Ext_SB_Fmt
            "\"}\n",
            file, line, name, val->size, val->capacity, Ext_SB_Arg(*val));
    return val;
}
#define DEFINE_PTR_DBG(type, namepart, fmt)                                                    \
    static inline type *ext_dbg_ptr_##namepart(const char *n, const char *f, int l, type *v) { \
        fprintf(stderr, "%s:%d: %s = (%s *) %p -> " fmt "\n", f, l, n, #type, (void *)v,       \
                v ? *v : 0);                                                                   \
        return v;                                                                              \
    }                                                                                          \
    static inline const type *ext_dbg_cptr_##namepart(const char *n, const char *f, int l,     \
                                                      const type *v) {                         \
        fprintf(stderr, "%s:%d: %s = (const %s *) %p -> " fmt "\n", f, l, n, #type,            \
                (const void *)v, v ? *v : 0);                                                  \
        return v;                                                                              \
    }
DEFINE_PTR_DBG(char, char, "'%c'")
DEFINE_PTR_DBG(signed char, signed_char, "%hhd")
DEFINE_PTR_DBG(unsigned char, unsigned_char, "%hhu")
DEFINE_PTR_DBG(short, short, "%hd")
DEFINE_PTR_DBG(unsigned short, unsigned_short, "%hu")
DEFINE_PTR_DBG(int, int, "%d")
DEFINE_PTR_DBG(unsigned int, unsigned_int, "%u")
DEFINE_PTR_DBG(long, long, "%ld")
DEFINE_PTR_DBG(unsigned long, unsigned_long, "%lu")
DEFINE_PTR_DBG(long long, long_long, "%lld")
DEFINE_PTR_DBG(unsigned long long, unsigned_long_long, "%llu")
DEFINE_PTR_DBG(float, float, "%f")
DEFINE_PTR_DBG(double, double, "%f")
DEFINE_PTR_DBG(long double, long_double, "%Lf")
static inline int ext_dbg_unknown(const char *name, const char *file, int line, ...) {
    fprintf(stderr, "%s:%d: %s = <unknown type>\n", file, line, name);
    return 0;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__
#endif  // ((defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || defined(__GNUC__))
        // && !defined(EXTLIB_NO_STD)

#ifndef EXTLIB_NO_SHORTHANDS
#define ASSERT        EXT_ASSERT
#define UNREACHABLE   EXT_UNREACHABLE
#define STATIC_ASSERT EXT_STATIC_ASSERT
#define TODO          EXT_TODO
#define DBG           EXT_DBG
#define ALIGN_PAD     EXT_ALIGN_PAD
#define ALIGN_UP      EXT_ALIGN_UP
#define ARR_SIZE      EXT_ARR_SIZE
#define KiB           EXT_KiB
#define MiB           EXT_MiB
#define GiB           EXT_GiB
#define PRINTF_FORMAT EXT_PRINTF_FORMAT
#define defer_loop    ext_defer_loop
#define DEFER_LOOP    EXT_DEFER_LOOP
#define return_exit   ext_return_exit

#define DEBUG      EXT_DEBUG
#define INFO       EXT_INFO
#define WARNING    EXT_WARNING
#define ERROR      EXT_ERROR
#define NO_LOGGING EXT_NO_LOGGING

#define Context                Ext_Context
#define PUSH_CONTEXT           EXT_PUSH_CONTEXT
#define PUSH_ALLOCATOR         EXT_PUSH_ALLOCATOR
#define LOGGING_LEVEL          EXT_LOGGING_LEVEL
#define push_context_allocator ext_push_context_allocator
#define push_context           ext_push_context
#define pop_context            ext_pop_context

#define Allocator              Ext_Allocator
#define DefaultAllocator       Ext_DefaultAllocator
#define default_allocator      ext_default_allocator
#define allocator_alloc        ext_allocator_alloc
#define allocator_realloc      ext_allocator_realloc
#define allocator_free         ext_allocator_free
#define allocator_strdup       ext_allocator_strdup
#define allocator_memdup       ext_allocator_memdup
#define allocator_new          ext_allocator_new
#define allocator_new_array    ext_allocator_new_array
#define allocator_delete       ext_allocator_delete
#define allocator_delete_array ext_allocator_delete_array
#define allocator_clone        ext_allocator_clone

#define TempAllocator   Ext_TempAllocator
#define temp_allocator  ext_temp_allocator
#define temp_set_mem    ext_temp_set_mem
#define temp_alloc      ext_temp_alloc
#define temp_realloc    ext_temp_realloc
#define temp_available  ext_temp_available
#define temp_reset      ext_temp_reset
#define temp_checkpoint ext_temp_checkpoint
#define temp_rewind     ext_temp_rewind
#define temp_strdup     ext_temp_strdup
#define temp_memdup     ext_temp_memdup
#ifndef EXTLIB_NO_STD
#define temp_sprintf  ext_temp_sprintf
#define temp_vsprintf ext_temp_vsprintf
#endif  // EXTLIB_NO_STD

#define ArenaFlags            Ext_ArenaFlags
#define ARENA_ZERO_ALLOC      EXT_ARENA_ZERO_ALLOC
#define ARENA_FIXED_PAGE_SIZE EXT_ARENA_FIXED_PAGE_SIZE
#define ARENA_NO_CHAIN        EXT_ARENA_NO_CHAIN
#define Arena                 Ext_Arena
#define ArenaPage             Ext_ArenaPage
#define ArenaCheckpoint       Ext_ArenaCheckpoint
#define make_arena            ext_make_arena
#define arena_alloc           ext_arena_alloc
#define arena_realloc         ext_arena_realloc
#define arena_free            ext_arena_free
#define arena_push            ext_arena_push
#define arena_push_array      ext_arena_push_array
#define arena_pop             ext_arena_pop
#define arena_pop_array       ext_arena_pop_array
#define arena_checkpoint      ext_arena_checkpoint
#define arena_rewind          ext_arena_rewind
#define arena_reset           ext_arena_reset
#define arena_destroy         ext_arena_destroy
#define arena_get_allocated   ext_arena_get_allocated
#define arena_strdup          ext_arena_strdup
#define arena_memdup          ext_arena_memdup
#ifndef EXTLIB_NO_STD
#define arena_sprintf  ext_arena_sprintf
#define arena_vsprintf ext_arena_vsprintf
#endif  // EXTLIB_NO_STD

#define array_foreach       ext_array_foreach
#define array_reserve       ext_array_reserve
#define array_reserve_exact ext_array_reserve_exact
#define array_push          ext_array_push
#define array_free          ext_array_free
#define array_push_all      ext_array_push_all
#define array_pop           ext_array_pop
#define array_remove        ext_array_remove
#define array_swap_remove   ext_array_swap_remove
#define array_clear         ext_array_clear
#define array_resize        ext_array_resize
#define array_shrink_to_fit ext_array_shrink_to_fit

#define StringBuffer     Ext_StringBuffer
#define SB_Fmt           Ext_SB_Fmt
#define SB_Arg           Ext_SB_Arg
#define sb_free          ext_sb_free
#define sb_append_char   ext_sb_append_char
#define sb_append        ext_sb_append
#define sb_append_cstr   ext_sb_append_cstr
#define sb_prepend       ext_sb_prepend
#define sb_prepend_cstr  ext_sb_prepend_cstr
#define sb_prepend_char  ext_sb_prepend_char
#define sb_reserve       ext_sb_reserve
#define sb_reserve_exact ext_sb_reserve_exact
#define sb_replace       ext_sb_replace
#define sb_to_upper      ext_sb_to_upper
#define sb_to_lower      ext_sb_to_lower
#define sb_reverse       ext_sb_reverse
#define sb_to_cstr       ext_sb_to_cstr
#ifndef EXTLIB_NO_STD
#define sb_appendf  ext_sb_appendf
#define sb_appendvf ext_sb_appendvf
#endif  // EXTLIB_NO_STD
#define sb_append_path      ext_sb_append_path
#define sb_append_path_cstr ext_sb_append_path_cstr

#define StringSlice                     Ext_StringSlice
#define ss_foreach_split                ext_ss_foreach_split
#define ss_foreach_rsplit               ext_ss_foreach_rsplit
#define ss_foreach_split_cstr           ext_ss_foreach_split_cstr
#define ss_foreach_rsplit_cstr          ext_ss_foreach_rsplit_cstr
#define SS_Fmt                          Ext_SS_Fmt
#define SS_Arg                          Ext_SS_Arg
#define SS                              Ext_SS
#define sb_to_ss                        ext_sb_to_ss
#define ss_from                         ext_ss_from
#define ss_from_cstr                    ext_ss_from_cstr
#define ss_split_once                   ext_ss_split_once
#define ss_rsplit_once                  ext_ss_rsplit_once
#define ss_split_once_any               ext_ss_split_once_any
#define ss_rsplit_once_any              ext_ss_rsplit_once_any
#define ss_split_once_ws                ext_ss_split_once_ws
#define ss_rsplit_once_ws               ext_ss_rsplit_once_ws
#define ss_split_once_cstr              ext_ss_split_once_cstr
#define ss_rsplit_once_cstr             ext_ss_rsplit_once_cstr
#define ss_split_once_ws                ext_ss_split_once_ws
#define ss_find_char                    ext_ss_find_char
#define ss_rfind_char                   ext_ss_rfind_char
#define ss_find                         ext_ss_find
#define ss_rfind                        ext_ss_rfind
#define ss_find_cstr                    ext_ss_find_cstr
#define ss_rfind_cstr                   ext_ss_rfind_cstr
#define ss_trim_start                   ext_ss_trim_start
#define ss_cut                          ext_ss_cut
#define ss_trunc                        ext_ss_trunc
#define ss_substr                       ext_ss_substr
#define ss_starts_with                  ext_ss_starts_with
#define ss_ends_with                    ext_ss_ends_with
#define ss_strip_prefix                 ext_ss_strip_prefix
#define ss_strip_suffix                 ext_ss_strip_suffix
#define ss_strip_prefix_cstr            ext_ss_strip_prefix_cstr
#define ss_strip_suffix_cstr            ext_ss_strip_suffix_cstr
#define ss_trim_end                     ext_ss_trim_end
#define ss_trim                         ext_ss_trim
#define ss_cmp                          ext_ss_cmp
#define ss_eq                           ext_ss_eq
#define ss_eq_ignore_case               ext_ss_eq_ignore_case
#define ss_cmp_ignore_case              ext_ss_cmp_ignore_case
#define ss_starts_with_ignore_case      ext_ss_starts_with_ignore_case
#define ss_ends_with_ignore_case        ext_ss_ends_with_ignore_case
#define ss_starts_with_ignore_case_cstr ext_ss_starts_with_ignore_case_cstr
#define ss_ends_with_ignore_case_cstr   ext_ss_ends_with_ignore_case_cstr
#define ss_to_cstr                      ext_ss_to_cstr
#define ss_to_cstr_temp                 ext_ss_to_cstr_temp
#define ss_to_cstr_alloc                ext_ss_to_cstr_alloc
#define ss_basename                     ext_ss_basename
#define ss_dirname                      ext_ss_dirname
#define ss_extension                    ext_ss_extension

#ifndef EXTLIB_NO_STD
#define Paths                  Ext_Paths
#define free_paths             ext_free_paths
#define FileType               Ext_FileType
#define FILE_ERR               EXT_FILE_ERR
#define FILE_REGULAR           EXT_FILE_REGULAR
#define FILE_SYMLINK           EXT_FILE_SYMLINK
#define FILE_DIR               EXT_FILE_DIR
#define FILE_OTHER             EXT_FILE_OTHER
#define read_file              ext_read_file
#define write_file             ext_write_file
#define read_line              ext_read_line
#define read_dir               ext_read_dir
#define create_dir             ext_create_dir
#define delete_dir_recursively ext_delete_dir_recursively
#define get_file_type          ext_get_file_type
#define rename_file            ext_rename_file
#define delete_file            ext_delete_file
#define get_cwd                ext_get_cwd
#define get_cwd_alloc          ext_get_cwd_alloc
#define get_cwd_temp           ext_get_cwd_temp
#define set_cwd                ext_set_cwd
#define get_abs_path_alloc     ext_get_abs_path_alloc
#define get_abs_path_temp      ext_get_abs_path_temp
#define get_abs_path           ext_get_abs_path
#define cmd                    ext_cmd
#define cmd_read               ext_cmd_read
#define cmd_write              ext_cmd_write
#endif

#define hmap_foreach          ext_hmap_foreach
#define hmap_end              ext_hmap_end
#define hmap_begin            ext_hmap_begin
#define hmap_next             ext_hmap_next
#define hmap_put              ext_hmap_put
#define hmap_get              ext_hmap_get
#define hmap_get_default      ext_hmap_get_default
#define hmap_delete           ext_hmap_delete
#define hmap_put_cstr         ext_hmap_put_cstr
#define hmap_get_cstr         ext_hmap_get_cstr
#define hmap_get_default_cstr ext_hmap_get_default_cstr
#define hmap_delete_cstr      ext_hmap_delete_cstr
#define hmap_put_ss           ext_hmap_put_ss
#define hmap_get_ss           ext_hmap_get_ss
#define hmap_get_default_ss   ext_hmap_get_default_ss
#define hmap_delete_ss        ext_hmap_delete_ss
#define hmap_clear            ext_hmap_clear
#define hmap_free             ext_hmap_free
#endif  // EXTLIB_NO_SHORTHANDS

#endif  // EXTLIB_H

/**
 * MIT License
 *
 * Copyright (c) 2025 Fabrizio Pietrucci
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
