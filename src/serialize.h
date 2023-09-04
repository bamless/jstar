#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <stdbool.h>

#include "jstar.h"
#include "object.h"

#define SERIALIZED_HEADER    "\xb5JsrC"
#define SERIALIZED_HEADER_SZ (sizeof(SERIALIZED_HEADER) - 1)

JStarBuffer serialize(JStarVM* vm, ObjFunction* f);
JStarResult deserialize(JStarVM* vm, ObjModule* mod, const void* code, size_t len,
                        ObjFunction** out);
bool isCompiledCode(const void* code, size_t len);

#endif
