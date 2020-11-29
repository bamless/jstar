#ifndef SERIALIZE_H
#define SERIALIZE_H

#include "jstar.h"
#include "object.h"

void* serialize(JStarVM* vm, ObjFunction* f, size_t* outSize);
ObjFunction* deserialize(JStarVM* vm, void* blob, size_t size);

#endif  // SERIALIZE_H
