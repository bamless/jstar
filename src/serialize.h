#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <stdbool.h>
#include <stddef.h>

#include "jstar.h"
#include "object_types.h"

JStarBuffer serialize(JStarVM* vm, ObjFunction* f);
JStarResult deserialize(JStarVM* vm, ObjModule* mod, const void* code, size_t len,
                        ObjFunction** out);
bool isCompiledCode(const void* code, size_t len);

#endif
