#include "serialize.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "code.h"
#include "object.h"
#include "util.h"
#include "value.h"

#define FILE_HEADER         "\xb5JsrC"
#define SERIALIZER_DEF_SIZE 64

#if(defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)
    #define __WINDOWS__
#endif

#if defined(__linux__) || defined(__CYGWIN__)
    #include <endian.h>
#elif defined(__APPLE__)
    #include <libkern/OSByteOrder.h>

    #define htobe16(x) OSSwapHostToBigInt16(x)
    #define htole16(x) OSSwapHostToLittleInt16(x)
    #define be16toh(x) OSSwapBigToHostInt16(x)
    #define le16toh(x) OSSwapLittleToHostInt16(x)

    #define htobe32(x) OSSwapHostToBigInt32(x)
    #define htole32(x) OSSwapHostToLittleInt32(x)
    #define be32toh(x) OSSwapBigToHostInt32(x)
    #define le32toh(x) OSSwapLittleToHostInt32(x)

    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define htole64(x) OSSwapHostToLittleInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
    #define le64toh(x) OSSwapLittleToHostInt64(x)

    #define __BYTE_ORDER    BYTE_ORDER
    #define __BIG_ENDIAN    BIG_ENDIAN
    #define __LITTLE_ENDIAN LITTLE_ENDIAN
    #define __PDP_ENDIAN    PDP_ENDIAN
#elif defined(__OpenBSD__)
    #include <sys/endian.h>
#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
    #include <sys/endian.h>

    #define be16toh(x) betoh16(x)
    #define le16toh(x) letoh16(x)

    #define be32toh(x) betoh32(x)
    #define le32toh(x) letoh32(x)

    #define be64toh(x) betoh64(x)
    #define le64toh(x) letoh64(x)
#elif defined(__WINDOWS__)
    #include <sys/param.h>
    #include <winsock2.h>

    #if BYTE_ORDER == LITTLE_ENDIAN
        #define htobe16(x) htons(x)
        #define htole16(x) (x)
        #define be16toh(x) ntohs(x)
        #define le16toh(x) (x)

        #define htobe32(x) htonl(x)
        #define htole32(x) (x)
        #define be32toh(x) ntohl(x)
        #define le32toh(x) (x)

        #define htobe64(x) htonll(x)
        #define htole64(x) (x)
        #define be64toh(x) ntohll(x)
        #define le64toh(x) (x)
    #elif BYTE_ORDER == BIG_ENDIAN
    /* that would be xbox 360 */
        #define htobe16(x) (x)
        #define htole16(x) __builtin_bswap16(x)
        #define be16toh(x) (x)
        #define le16toh(x) __builtin_bswap16(x)

        #define htobe32(x) (x)
        #define htole32(x) __builtin_bswap32(x)
        #define be32toh(x) (x)
        #define le32toh(x) __builtin_bswap32(x)

        #define htobe64(x) (x)
        #define htole64(x) __builtin_bswap64(x)
        #define be64toh(x) (x)
        #define le64toh(x) __builtin_bswap64(x)

    #else
        #error byte order not supported
    #endif

    #define __BYTE_ORDER    BYTE_ORDER
    #define __BIG_ENDIAN    BIG_ENDIAN
    #define __LITTLE_ENDIAN LITTLE_ENDIAN
    #define __PDP_ENDIAN    PDP_ENDIAN
#else
    #error platform not supported
#endif

typedef enum SeriaLizedValue {
    SERIALIZED_NUM,
    SERIALIZED_BOOL,
    SERIALIZED_OBJ,
    SERIALIZED_NULL,
    SERIALIZED_HANDLE,
} SeriaLizedValue;

typedef struct Serializer {
    JStarVM* vm;
    size_t size, count;
    uint8_t* data;
} Serializer;

static void initSerializer(Serializer* s, JStarVM* vm) {
    s->vm = vm;
    s->size = SERIALIZER_DEF_SIZE;
    s->count = 0;
    s->data = malloc(SERIALIZER_DEF_SIZE);
}

static bool shouldGrow(Serializer* s, size_t bytes) {
    return s->count + bytes > s->size;
}

static void reserve(Serializer* s, size_t bytes) {
    while(shouldGrow(s, bytes)) {
        s->size *= 2;
    }
    s->data = realloc(s->data, s->size);
}

static void write(Serializer* s, const void* data, size_t size) {
    reserve(s, size);
    memcpy(s->data + s->count, data, size);
    s->count += size;
}

static void serializeUint64(Serializer* s, uint64_t num) {
    uint64_t bigendian = htobe64(num);
    write(s, &bigendian, sizeof(uint64_t));
}

static void serializeUint32(Serializer* s, uint32_t num) {
    uint32_t bigendian = htobe32(num);
    write(s, &bigendian, sizeof(uint32_t));
}

static void serializeByte(Serializer* s, uint8_t byte) {
    write(s, &byte, sizeof(uint8_t));
}

static void serializeCString(Serializer* s, const char* string) {
    write(s, string, strlen(string));
}

static void serializeDouble(Serializer* s, double num) {
    struct {
        double num;
        uint64_t raw;
    } convert = {.num = num};
    serializeUint64(s, convert.raw);
}

static void serializeString(Serializer* s, ObjString* str) {
    serializeUint64(s, (uint64_t)str->length);
    write(s, str->data, str->length);
}

static void serializeConst(Serializer* s, Value c) {
    if(IS_NUM(c)) {
        serializeByte(s, SERIALIZED_NUM);
        serializeDouble(s, AS_NUM(c));
    } else if(IS_BOOL(c)) {
        serializeByte(s, SERIALIZED_BOOL);
        serializeByte(s, AS_BOOL(c));
    } else if(IS_NULL(c)) {
        serializeByte(s, SERIALIZED_NULL);
    } else if(IS_HANDLE(c)) {
        serializeByte(s, SERIALIZED_HANDLE);
    } else {
        UNREACHABLE();
    }
}

static void serializeCommon(Serializer* s, FnCommon* c) {
    serializeByte(s, c->argsCount);
    serializeByte(s, c->vararg);

    if(c->name) {
        serializeString(s, c->name);
    } else {
        serializeUint64(s, 0);
    }

    serializeByte(s, c->defCount);
    for(int i = 0; i < c->defCount; i++) {
        serializeConst(s, c->defaults[i]);
    }
}

static void serializeCode(Serializer* s, Code* c) {
    // serialize lines
    serializeUint64(s, c->linesCount);
    for(size_t i = 0; i < c->count; i++) {
        serializeUint32(s, c->bytecode[i]);
    }

    // serialize bytecode
    serializeUint64(s, c->count);
    for(size_t i = 0; i < c->count; i++) {
        serializeByte(s, c->bytecode[i]);
    }
}

static void serializeFunction(Serializer* s, ObjFunction* f) {
    serializeCommon(s, &f->c);
    serializeCode(s, &f->code);
}

void* serialize(JStarVM* vm, ObjFunction* f, size_t* outSize) {
    Serializer s;
    initSerializer(&s, vm);

    serializeCString(&s, FILE_HEADER);
    serializeByte(&s, JSTAR_VERSION_MAJOR);
    serializeByte(&s, JSTAR_VERSION_MINOR);
    serializeFunction(&s, f);

    *outSize = s.count;
    return realloc(s.data, s.count);
}
