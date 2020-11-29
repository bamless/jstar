#include "serialize.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "code.h"
#include "object.h"
#include "util.h"
#include "value.h"
#include "vm.h"

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
    SER_NUM,
    SER_BOOL,
    SER_OBJ,
    SER_NULL,
    SER_HANDLE,
    SER_OBJ_STR,
    SER_OBJ_FUN,
    SER_OBJ_NAT,
} SeriaLizedValue;

static void write(JStarBuffer* buf, const void* data, size_t size) {
    jsrBufferAppend(buf, (const char*)data, size);
}

static void serializeUint64(JStarBuffer* buf, uint64_t num) {
    uint64_t bigendian = htobe64(num);
    write(buf, &bigendian, sizeof(uint64_t));
}

static void serializeUint32(JStarBuffer* buf, uint32_t num) {
    uint32_t bigendian = htobe32(num);
    write(buf, &bigendian, sizeof(uint32_t));
}

static void serializeShort(JStarBuffer* buf, uint16_t num) {
    uint16_t bigendian = htobe16(num);
    write(buf, &bigendian, sizeof(uint16_t));
}

static void serializeByte(JStarBuffer* buf, uint8_t byte) {
    write(buf, &byte, sizeof(uint8_t));
}

static void serializeCString(JStarBuffer* buf, const char* string) {
    write(buf, string, strlen(string));
}

static void serializeDouble(JStarBuffer* buf, double num) {
    struct {
        double num;
        uint64_t raw;
    } convert = {.num = num};
    serializeUint64(buf, convert.raw);
}

static void serializeString(JStarBuffer* buf, ObjString* str) {
    bool isShort = str->length <= UINT8_MAX;
    serializeByte(buf, isShort);

    if(isShort)
        serializeByte(buf, (uint8_t)str->length);
    else
        serializeUint64(buf, (uint64_t)str->length);

    write(buf, str->data, str->length);
}

static void serializeConst(JStarBuffer* buf, Value c) {
    if(IS_NUM(c)) {
        serializeByte(buf, SER_NUM);
        serializeDouble(buf, AS_NUM(c));
    } else if(IS_BOOL(c)) {
        serializeByte(buf, SER_BOOL);
        serializeByte(buf, AS_BOOL(c));
    } else if(IS_NULL(c)) {
        serializeByte(buf, SER_NULL);
    } else if(IS_STRING(c)) {
        serializeByte(buf, SER_OBJ_STR);
        serializeString(buf, AS_STRING(c));
    } else if(IS_HANDLE(c)) {
        serializeByte(buf, SER_HANDLE);
    } else {
        UNREACHABLE();
    }
}

static void serializeCommon(JStarBuffer* buf, FnCommon* c) {
    serializeByte(buf, c->argsCount);
    serializeByte(buf, c->vararg);

    if(c->name) {
        serializeString(buf, c->name);
    } else {
        serializeShort(buf, 0);
    }

    serializeByte(buf, c->defCount);
    for(int i = 0; i < c->defCount; i++) {
        serializeConst(buf, c->defaults[i]);
    }
}

static void serializeFunction(JStarBuffer* buf, ObjFunction* f);

static void serializeNative(JStarBuffer* buf, ObjNative* n) {
    serializeCommon(buf, &n->c);
}

static void serializeConstants(JStarBuffer* buf, ValueArray* constants) {
    serializeShort(buf, constants->count);
    for(int i = 0; i < constants->count; i++) {
        Value c = constants->arr[i];
        if(IS_FUNC(c)) {
            serializeByte(buf, SER_OBJ_FUN);
            serializeFunction(buf, AS_FUNC(c));
        } else if(IS_NATIVE(c)) {
            serializeByte(buf, SER_OBJ_NAT);
            serializeNative(buf, AS_NATIVE(c));
        } else {
            serializeConst(buf, c);
        }
    }
}

static void serializeCode(JStarBuffer* buf, Code* c) {
    // TODO: store (compressed) line information? maybe give option in application
    // serialize lines
    // serializeUint64(buf, c->linesCount);
    // for(size_t i = 0; i < c->count; i++) {
    //     serializeUint32(buf, c->lines[i]);
    // }

    // serialize bytecode
    serializeUint64(buf, c->count);
    for(size_t i = 0; i < c->count; i++) {
        serializeByte(buf, c->bytecode[i]);
    }

    serializeConstants(buf, &c->consts);
}

static void serializeFunction(JStarBuffer* buf, ObjFunction* f) {
    serializeCommon(buf, &f->c);
    serializeCode(buf, &f->code);
}

JStarBuffer serialize(JStarVM* vm, ObjFunction* f) {
    // Push as gc root
    push(vm, OBJ_VAL(f));

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, SERIALIZER_DEF_SIZE);

    serializeCString(&buf, FILE_HEADER);
    serializeByte(&buf, JSTAR_VERSION_MAJOR);
    serializeByte(&buf, JSTAR_VERSION_MINOR);
    serializeFunction(&buf, f);

    jsrBufferShrinkToFit(&buf);
    pop(vm);

    return buf;
}
