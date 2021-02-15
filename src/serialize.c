#include "serialize.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "code.h"
#include "endianness.h"
#include "gc.h"
#include "object.h"
#include "util.h"
#include "value.h"
#include "vm.h"

#define SER_DEF_SIZE 64

typedef enum ConstType {
    CONST_NUM = 1,
    CONST_BOOL = 2,
    CONST_NULL = 3,
    CONST_STR = 4,
    CONST_FUN = 5,
    CONST_NAT = 6
} ConstType;

// -----------------------------------------------------------------------------
// SERIALIZATION
// -----------------------------------------------------------------------------

static void write(JStarBuffer* buf, const void* data, size_t size) {
    jsrBufferAppend(buf, (const char*)data, size);
}

static void serializeUint64(JStarBuffer* buf, uint64_t num) {
    uint64_t bigendian = htobe64(num);
    write(buf, &bigendian, sizeof(uint64_t));
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
    serializeUint64(buf, REINTERPRET_CAST(double, uint64_t, num));
}

static void serializeString(JStarBuffer* buf, ObjString* str) {
    bool isShort = str->length <= UINT8_MAX;
    serializeByte(buf, isShort);
    if(isShort) {
        serializeByte(buf, (uint8_t)str->length);
    } else {
        serializeUint64(buf, (uint64_t)str->length);
    }
    write(buf, str->data, str->length);
}

static void serializeConstLiteral(JStarBuffer* buf, Value c) {
    if(IS_NUM(c)) {
        serializeByte(buf, CONST_NUM);
        serializeDouble(buf, AS_NUM(c));
    } else if(IS_BOOL(c)) {
        serializeByte(buf, CONST_BOOL);
        serializeByte(buf, AS_BOOL(c));
    } else if(IS_NULL(c)) {
        serializeByte(buf, CONST_NULL);
    } else if(IS_STRING(c)) {
        serializeByte(buf, CONST_STR);
        serializeString(buf, AS_STRING(c));
    } else {
        UNREACHABLE();
    }
}

static void serializeCommon(JStarBuffer* buf, FnCommon* c) {
    serializeByte(buf, c->argsCount);
    serializeByte(buf, c->vararg);

    serializeString(buf, c->name);

    serializeByte(buf, c->defCount);
    for(int i = 0; i < c->defCount; i++) {
        serializeConstLiteral(buf, c->defaults[i]);
    }
}

static void serializeFunction(JStarBuffer* buf, ObjFunction* f);

static void serializeNative(JStarBuffer* buf, ObjNative* n) {
    serializeCommon(buf, &n->c);
}

static void serializeConstants(JStarBuffer* buf, ValueArray* consts) {
    serializeShort(buf, consts->count);
    for(int i = 0; i < consts->count; i++) {
        Value c = consts->arr[i];
        if(IS_FUNC(c)) {
            serializeByte(buf, CONST_FUN);
            serializeFunction(buf, AS_FUNC(c));
        } else if(IS_NATIVE(c)) {
            serializeByte(buf, CONST_NAT);
            serializeNative(buf, AS_NATIVE(c));
        } else {
            serializeConstLiteral(buf, c);
        }
    }
}

static void serializeCode(JStarBuffer* buf, Code* c) {
    // TODO: store (compressed) line information? maybe give option in application

    // serialize bytecode
    serializeUint64(buf, c->count);
    for(size_t i = 0; i < c->count; i++) {
        serializeByte(buf, c->bytecode[i]);
    }

    serializeConstants(buf, &c->consts);
}

static void serializeFunction(JStarBuffer* buf, ObjFunction* f) {
    serializeCommon(buf, &f->c);
    serializeByte(buf, f->upvalueCount);
    serializeCode(buf, &f->code);
}

JStarBuffer serialize(JStarVM* vm, ObjFunction* fn) {
    // Push as gc root
    jsrEnsureStack(vm, 1);
    push(vm, OBJ_VAL(fn));

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, SER_DEF_SIZE);

    serializeCString(&buf, SER_FILE_HEADER);
    serializeByte(&buf, JSTAR_VERSION_MAJOR);
    serializeByte(&buf, JSTAR_VERSION_MINOR);
    serializeFunction(&buf, fn);

    jsrBufferShrinkToFit(&buf);
    pop(vm);

    return buf;
}

// -----------------------------------------------------------------------------
// DESERIALIZATION
// -----------------------------------------------------------------------------

typedef struct Deserializer {
    JStarVM* vm;
    const JStarBuffer* buf;
    ObjModule* mod;
    size_t ptr;
} Deserializer;

static bool read(Deserializer* d, void* dest, size_t size) {
    if(d->ptr + size > d->buf->capacity) {
        return false;
    }
    memcpy(dest, d->buf->data + d->ptr, size);
    d->ptr += size;
    return true;
}

static bool isExausted(Deserializer* d) {
    return d->ptr == d->buf->size;
}

static void zeroValueArray(Value* vals, int size) {
    for(int i = 0; i < size; i++) {
        vals[i] = NULL_VAL;
    }
}

static bool deserializeUint64(Deserializer* d, uint64_t* out) {
    uint64_t bigendian;
    if(!read(d, &bigendian, sizeof(uint64_t))) {
        return false;
    }
    *out = be64toh(bigendian);
    return true;
}

static bool deserializeShort(Deserializer* d, uint16_t* out) {
    uint16_t bigendian;
    if(!read(d, &bigendian, sizeof(uint16_t))) {
        return false;
    }
    *out = be16toh(bigendian);
    return true;
}

static bool deserializeByte(Deserializer* d, uint8_t* out) {
    return read(d, out, sizeof(uint8_t));
}

static bool deserializeCString(Deserializer* d, char* out, size_t size) {
    return read(d, out, size);
}

static bool deserializeString(Deserializer* d, ObjString** out) {
    uint8_t isShort;
    if(!deserializeByte(d, &isShort)) return false;

    uint64_t length;
    if(isShort) {
        uint8_t shortLength;
        if(!deserializeByte(d, &shortLength)) return false;
        length = shortLength;
    } else {
        if(!deserializeUint64(d, &length)) return false;
    }

    if(length <= 4096) {
        char str[4096];
        if(!deserializeCString(d, str, length)) return false;
        *out = copyString(d->vm, str, length);
    } else {
        char* str = calloc(length, 1);
        if(!deserializeCString(d, str, length)) {
            free(str);
            return false;
        }
        *out = copyString(d->vm, str, length);
        free(str);
    }

    return true;
}

static bool deserializeDouble(Deserializer* d, double* out) {
    uint64_t raw;
    if(!deserializeUint64(d, &raw)) return false;
    *out = REINTERPRET_CAST(uint64_t, double, raw);
    return true;
}

static bool deserializeConstLiteral(Deserializer* d, ConstType type, Value* out) {
    switch(type) {
    case CONST_NUM: {
        double num;
        if(!deserializeDouble(d, &num)) return false;
        *out = NUM_VAL(num);
        return true;
    }
    case CONST_BOOL: {
        uint8_t boolean;
        if(!deserializeByte(d, &boolean)) return false;
        *out = BOOL_VAL(boolean);
        return true;
    }
    case CONST_NULL: {
        *out = NULL_VAL;
        return true;
    }
    case CONST_STR: {
        ObjString* str;
        if(!deserializeString(d, &str)) return false;
        *out = OBJ_VAL(str);
        return true;
    }
    default:
        return false;
    }
}

static bool deserializeCommon(Deserializer* d, FnCommon* c) {
    if(!deserializeByte(d, &c->argsCount)) return false;

    uint8_t vararg;
    if(!deserializeByte(d, &vararg)) return false;
    c->vararg = (bool)vararg;

    if(!deserializeString(d, &c->name)) return false;

    uint8_t defCount;
    if(!deserializeByte(d, &defCount)) return false;

    c->defaults = GC_ALLOC(d->vm, sizeof(Value) * defCount);
    zeroValueArray(c->defaults, defCount);
    c->defCount = defCount;

    for(int i = 0; i < defCount; i++) {
        uint8_t valueType;
        if(!deserializeByte(d, &valueType)) return false;
        if(!deserializeConstLiteral(d, valueType, &c->defaults[i])) return false;
    }

    return true;
}

static bool deserializeFunction(Deserializer* d, ObjFunction** out);

static bool deserializeNative(Deserializer* d, ObjNative** out) {
    JStarVM* vm = d->vm;
    ObjModule* mod = d->mod;

    // Create native and push it as root in case a gc is triggered
    jsrEnsureStack(vm, 1);
    ObjNative* nat = newNative(vm, mod, 0, 0, false);
    push(vm, OBJ_VAL(nat));

    if(!deserializeCommon(d, &nat->c)) {
        pop(vm);
        return false;
    }

    *out = nat;
    pop(vm);

    return true;
}

static bool deserializeConstants(Deserializer* d, ValueArray* consts) {
    uint16_t constantSize;
    if(!deserializeShort(d, &constantSize)) return false;

    consts->arr = malloc(sizeof(Value) * constantSize);
    zeroValueArray(consts->arr, constantSize);
    consts->count = constantSize;
    consts->size = constantSize;

    for(int i = 0; i < constantSize; i++) {
        uint8_t constType;
        if(!deserializeByte(d, &constType)) return false;

        switch((ConstType)constType) {
        case CONST_FUN: {
            ObjFunction* fn;
            if(!deserializeFunction(d, &fn)) return false;
            consts->arr[i] = OBJ_VAL(fn);
            break;
        }
        case CONST_NAT: {
            ObjNative* nat;
            if(!deserializeNative(d, &nat)) return false;
            consts->arr[i] = OBJ_VAL(nat);
            break;
        }
        default:
            if(!deserializeConstLiteral(d, constType, &consts->arr[i])) return false;
            break;
        }
    }

    return true;
}

static bool deserializeCode(Deserializer* d, Code* c) {
    uint64_t codeSize;
    if(!deserializeUint64(d, &codeSize)) return false;

    c->bytecode = malloc(codeSize);
    c->count = codeSize;
    c->size = codeSize;

    if(!read(d, c->bytecode, codeSize)) return false;
    if(!deserializeConstants(d, &c->consts)) return false;

    return true;
}

static bool deserializeFunction(Deserializer* d, ObjFunction** out) {
    JStarVM* vm = d->vm;
    ObjModule* mod = d->mod;

    // Create function and push it as root in case a gc is triggered
    jsrEnsureStack(vm, 1);
    ObjFunction* fn = newFunction(vm, mod, 0, 0, false);
    push(vm, OBJ_VAL(fn));

    if(!deserializeCommon(d, &fn->c)) {
        pop(vm);
        return false;
    }

    if(!deserializeByte(d, &fn->upvalueCount)) {
        pop(vm);
        return false;
    }

    if(!deserializeCode(d, &fn->code)) {
        pop(vm);
        return false;
    }

    *out = fn;
    pop(vm);

    return true;
}

ObjFunction* deserialize(JStarVM* vm, ObjModule* mod, const JStarBuffer* buf, JStarResult* err) {
    ASSERT(vm == buf->vm, "JStarBuffer isn't owned by provided vm");
    Deserializer d = {vm, buf, mod, 0};

    *err = JSR_DESERIALIZE_ERR;

    char header[SER_HEADER_SIZE];
    if(!read(&d, header, SER_HEADER_SIZE)) return NULL;
    ASSERT(memcmp(header, SER_FILE_HEADER, SER_HEADER_SIZE) == 0, "Header error");

    uint8_t versionMajor, versionMinor;
    if(!deserializeByte(&d, &versionMajor)) return NULL;
    if(!deserializeByte(&d, &versionMinor)) return NULL;

    if(versionMajor != JSTAR_VERSION_MAJOR || versionMinor != JSTAR_VERSION_MINOR) {
        *err = JSR_VERSION_ERR;
        return NULL;
    }

    ObjFunction* fn;
    if(!deserializeFunction(&d, &fn)) {
        return NULL;
    }

    if(!isExausted(&d)) {
        return NULL;
    }

    *err = JSR_SUCCESS;
    return fn;
}

bool isCompiledCode(const JStarBuffer* buf) {
    if(buf->size >= SER_HEADER_SIZE) {
        return memcmp(SER_FILE_HEADER, buf->data, SER_HEADER_SIZE) == 0;
    }
    return false;
}
