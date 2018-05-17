#ifndef CORE_H
#define CORE_H

#include "native.h"

typedef struct VM VM;

void initCoreLibrary(VM *vm);

NATIVE(bl_error);
NATIVE(bl_isInt);
NATIVE(bl_str);

// class List {

NATIVE(bl_List_append);
NATIVE(bl_List_insert);
NATIVE(bl_List_length);
NATIVE(bl_List_remove);

// } List

// class String {

NATIVE(bl_String_length);

// } String

#endif
