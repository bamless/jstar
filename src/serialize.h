#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <stdbool.h>

#include "jstar.h"
#include "object.h"

#define SER_FILE_HEADER "\xb5JsrC"
#define SER_HEADER_SIZE (sizeof(SER_FILE_HEADER) - 1)

JStarBuffer serialize(JStarVM* vm, ObjFunction* f);
ObjFunction* deserialize(JStarVM* vm, ObjModule* mod, const JStarBuffer* buf, JStarResult* err);
bool isCompiledCode(const JStarBuffer* buf);

#endif
