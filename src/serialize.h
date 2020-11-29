#ifndef SERIALIZE_H
#define SERIALIZE_H

#include "jstar.h"
#include "object.h"

JStarBuffer serialize(JStarVM* vm, ObjFunction* f);
ObjFunction* deserialize(JStarVM* vm, const JStarBuffer* buf);

#endif  // SERIALIZE_H