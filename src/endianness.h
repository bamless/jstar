#ifndef ENDIANNES_H
#define ENDIANNES_H

#include "jstarconf.h"

// -----------------------------------------------------------------------------
// ENDIANNESS MACROS
// -----------------------------------------------------------------------------

#if defined(JSTAR_LINUX) || defined(JSTAR_EMSCRIPTEN)
    #include <endian.h>
#elif defined(JSTAR_MACOS) || defined(JSTAR_IOS)
    #include <libkern/OSByteOrder.h>

    #define htobe16(x) OSSwapHostToBigInt16(x)
    #define be16toh(x) OSSwapBigToHostInt16(x)

    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(JSTAR_OPENBSD)
    #include <sys/endian.h>
#elif defined(JSTAR_FREEBSD)
    #include <sys/endian.h>

    #define be16toh(x) betoh16(x)
    #define be64toh(x) betoh64(x)
#elif defined(JSTAR_WINDOWS)
    #if BYTE_ORDER == LITTLE_ENDIAN
        #if defined(_MSC_VER)
            #include <stdlib.h>

            #define htobe16(x) _byteswap_ushort(x)
            #define be16toh(x) _byteswap_ushort(x)

            #define htobe64(x) _byteswap_uint64(x)
            #define be64toh(x) _byteswap_uint64(x)
        #elif defined(__GNUC__)
            #define htobe16(x) __builtin_bswap16(x)
            #define be16toh(x) __builtin_bswap16(x)

            #define htobe64(x) __builtin_bswap64(x)
            #define be64toh(x) __builtin_bswap64(x)
        #else
            #error Unsupported compiler: unknown endianness conversion functions
        #endif
    #elif BYTE_ORDER == BIG_ENDIAN
        #define htobe16(x) (x)
        #define be16toh(x) (x)

        #define htobe64(x) (x)
        #define be64toh(x) (x)
    #else
        #error Unsupported platform: unknown endiannes
    #endif
#else
    #error Unsupported platform: unknown endiannes
#endif

#endif
