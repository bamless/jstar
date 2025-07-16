#include "serialize.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "code.h"
#include "endianness.h"
#include "gc.h"
#include "jstar.h"
#include "object.h"
#include "profiler.h"
#include "symbol.h"
#include "util.h"
#include "value.h"
#include "vm.h"

#define SER_DEF_SIZE    64
#define STATIC_STR_SIZE 4096
#define HEADER_MAGIC    0xb5

static const uint8_t HEADER[] = {'J', 's', 'r', 'C'};

typedef struct Header {
    uint8_t magic;
    uint8_t header[sizeof(HEADER)];
} Header;

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
        JSR_UNREACHABLE();
    }
}

static void serializePrototype(JStarBuffer* buf, Prototype* proto) {
    serializeByte(buf, proto->argsCount);
    serializeByte(buf, proto->vararg);

    serializeString(buf, proto->name);

    serializeByte(buf, proto->defCount);
    for(int i = 0; i < proto->defCount; i++) {
        serializeConstLiteral(buf, proto->defaults[i]);
    }
}

static void serializeFunction(JStarBuffer* buf, ObjFunction* f);

static void serializeNative(JStarBuffer* buf, ObjNative* n) {
    serializePrototype(buf, &n->proto);
}

static void serializeConstants(JStarBuffer* buf, Values consts) {
    serializeShort(buf, consts.count);
    for(size_t i = 0; i < consts.count; i++) {
        Value c = consts.items[i];
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

static void serializeSymbols(JStarBuffer* buf, Symbols symbols) {
    serializeShort(buf, symbols.count);
    for(size_t i = 0; i < symbols.count; i++) {
        serializeShort(buf, symbols.items[i].constant);
    }
}

static void serializeCode(JStarBuffer* buf, Code* c) {
    // TODO: store (compressed) line information? maybe give option in application

    // serialize bytecode
    serializeUint64(buf, c->bytecode.count);
    for(size_t i = 0; i < c->bytecode.count; i++) {
        serializeByte(buf, c->bytecode.items[i]);
    }

    serializeConstants(buf, c->consts);
    serializeSymbols(buf, c->symbols);
}

static void serializeFunction(JStarBuffer* buf, ObjFunction* f) {
    serializePrototype(buf, &f->proto);
    serializeByte(buf, f->upvalueCount);
    serializeShort(buf, f->stackUsage);
    serializeCode(buf, &f->code);
}

JStarBuffer serialize(JStarVM* vm, ObjFunction* fn) {
    PROFILE_FUNC()

    // Push as gc root
    jsrEnsureStack(vm, 1);
    push(vm, OBJ_VAL(fn));

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, SER_DEF_SIZE);

    Header h;
    h.magic = HEADER_MAGIC;
    memcpy(h.header, HEADER, sizeof(HEADER));

    write(&buf, &h, sizeof(h));
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
    const uint8_t* code;
    size_t len;
    ObjModule* mod;
    size_t ptr;
} Deserializer;

static bool read(Deserializer* d, void* dest, size_t size) {
    if(d->ptr + size > d->len) {
        return false;
    }
    memcpy(dest, d->code + d->ptr, size);
    d->ptr += size;
    return true;
}

static bool isExausted(Deserializer* d) {
    return d->ptr == d->len;
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

    if(length <= STATIC_STR_SIZE) {
        char str[STATIC_STR_SIZE];
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
    case CONST_FUN:
    case CONST_NAT:
        // CONST_FUN and CONST_NAT should be already handled in deserializeConstants
        return false;
    default:
        // Malformed constant type
        return false;
    }
}

static bool deserializePrototype(Deserializer* d, Prototype* proto) {
    if(!deserializeByte(d, &proto->argsCount)) return false;

    uint8_t vararg;
    if(!deserializeByte(d, &vararg)) return false;
    proto->vararg = (bool)vararg;

    if(!deserializeString(d, &proto->name)) return false;

    uint8_t defCount;
    if(!deserializeByte(d, &defCount)) return false;

    proto->defaults = GC_ALLOC(d->vm, sizeof(Value) * defCount);
    zeroValueArray(proto->defaults, defCount);
    proto->defCount = defCount;

    for(int i = 0; i < defCount; i++) {
        uint8_t valueType;
        if(!deserializeByte(d, &valueType)) return false;
        if(!deserializeConstLiteral(d, valueType, &proto->defaults[i])) return false;
    }

    return true;
}

static bool deserializeFunction(Deserializer* d, ObjFunction** out);

static bool deserializeNative(Deserializer* d, ObjNative** out) {
    JStarVM* vm = d->vm;
    ObjModule* mod = d->mod;

    // Create native and push it as root in case a gc is triggered
    jsrEnsureStack(vm, 1);
    ObjNative* nat = newNative(vm, mod, copyString(vm, "", 0), 0, 0, false, NULL);
    push(vm, OBJ_VAL(nat));

    if(!deserializePrototype(d, &nat->proto)) {
        pop(vm);
        return false;
    }

    *out = nat;
    pop(vm);

    return true;
}

static bool deserializeConstants(Deserializer* d, Values* consts) {
    uint16_t constsSize;
    if(!deserializeShort(d, &constsSize)) return false;

    arrayReserve(consts, constsSize);
    zeroValueArray(consts->items, constsSize);

    for(int i = 0; i < constsSize; i++) {
        uint8_t constType;
        if(!deserializeByte(d, &constType)) return false;

        switch((ConstType)constType) {
        case CONST_FUN: {
            ObjFunction* fn;
            if(!deserializeFunction(d, &fn)) return false;
            consts->items[consts->count++] = OBJ_VAL(fn);
            break;
        }
        case CONST_NAT: {
            ObjNative* nat;
            if(!deserializeNative(d, &nat)) return false;
            consts->items[consts->count++] = OBJ_VAL(nat);
            break;
        }
        default:
            if(!deserializeConstLiteral(d, constType, &consts->items[consts->count++])) {
                return false;
            }
            break;
        }
    }

    return true;
}

static bool deserializeSymbols(Deserializer* d, Symbols* s) {
    uint16_t symbolCount;
    if(!deserializeShort(d, &symbolCount)) return false;

    arrayReserve(s, symbolCount);
    for(int i = 0; i < symbolCount; i++) {
        uint16_t constant;
        if(!deserializeShort(d, &constant)) return false;
        s->items[s->count++] = (Symbol){.constant = constant};
    }

    return true;
}

static bool deserializeCode(Deserializer* d, Code* c) {
    uint64_t codeSize;
    if(!deserializeUint64(d, &codeSize)) return false;

    arrayReserve(&c->bytecode, codeSize);
    if(!read(d, c->bytecode.items, codeSize)) return false;
    c->bytecode.count = codeSize;

    if(!deserializeConstants(d, &c->consts)) return false;
    if(!deserializeSymbols(d, &c->symbols)) return false;

    return true;
}

static bool deserializeFunction(Deserializer* d, ObjFunction** out) {
    JStarVM* vm = d->vm;
    ObjModule* mod = d->mod;

    // Create function and push it as root in case a gc is triggered
    jsrEnsureStack(vm, 1);
    ObjFunction* fn = newFunction(vm, mod, copyString(vm, "", 0), 0, 0, false);
    push(vm, OBJ_VAL(fn));

    if(!deserializePrototype(d, &fn->proto)) {
        pop(vm);
        return false;
    }

    if(!deserializeByte(d, &fn->upvalueCount)) {
        pop(vm);
        return false;
    }

    uint16_t stackUsage;
    if(!deserializeShort(d, &stackUsage)) {
        pop(vm);
        return false;
    }
    fn->stackUsage = stackUsage;

    if(!deserializeCode(d, &fn->code)) {
        pop(vm);
        return false;
    }

    *out = fn;
    pop(vm);

    return true;
}

JStarResult deserialize(JStarVM* vm, ObjModule* mod, const void* code, size_t len,
                        ObjFunction** out) {
    PROFILE_FUNC()

    Deserializer d = {vm, code, len, mod, 0};

    Header h;
    if(!read(&d, &h, sizeof(h))) {
        return JSR_DESERIALIZE_ERR;
    }

    if(h.magic != HEADER_MAGIC && memcmp(h.header, HEADER, sizeof(HEADER)) != 0) {
        return JSR_DESERIALIZE_ERR;
    }

    uint8_t versionMajor, versionMinor;
    if(!deserializeByte(&d, &versionMajor) || !deserializeByte(&d, &versionMinor)) {
        return JSR_DESERIALIZE_ERR;
    }

    if(versionMajor != JSTAR_VERSION_MAJOR || versionMinor != JSTAR_VERSION_MINOR) {
        return JSR_VERSION_ERR;
    }

    if(!deserializeFunction(&d, out)) {
        return JSR_DESERIALIZE_ERR;
    }

    if(!isExausted(&d)) {
        return JSR_DESERIALIZE_ERR;
    }

    return JSR_SUCCESS;
}

bool isCompiledCode(const void* code, size_t len) {
    if(len < sizeof(Header)) {
        return false;
    }
    Header* h = (Header*)code;
    return h->magic == HEADER_MAGIC && memcmp(h->header, HEADER, sizeof(HEADER)) == 0;
}
