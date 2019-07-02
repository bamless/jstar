#ifndef CORE_H
#define CORE_H

#include "blang.h"

// Blang core libray bootstrap
void initCoreLibrary(BlangVM *vm);

NATIVE(bl_int);
NATIVE(bl_num);
NATIVE(bl_isInt);

NATIVE(bl_char);
NATIVE(bl_ascii);

NATIVE(bl_printstr);
NATIVE(bl_eval);

NATIVE(bl_type);

// class Number {
NATIVE(bl_Number_string);
NATIVE(bl_Number_hash);
// } Number

// class Boolean {
NATIVE(bl_Boolean_string);
// } Boolean

// class Null {
NATIVE(bl_Null_string);
// } Null

// class Function {
NATIVE(bl_Function_string);
// } Function

// class Module {
NATIVE(bl_Module_string);
// } Module

// class List {
NATIVE(bl_List_new);
NATIVE(bl_List_add);
NATIVE(bl_List_insert);
NATIVE(bl_List_len);
NATIVE(bl_List_removeAt);
NATIVE(bl_List_clear);
NATIVE(bl_List_subList);
NATIVE(bl_List_iter);
NATIVE(bl_List_next);
// } List

// class Tuple {
NATIVE(bl_Tuple_new);
NATIVE(bl_Tuple_len);
NATIVE(bl_Tuple_iter);
NATIVE(bl_Tuple_next);
NATIVE(bl_Tuple_sub);
// }

// class String {
NATIVE(bl_substr);
NATIVE(bl_String_len);
NATIVE(bl_String_string);
NATIVE(bl_String_join);
NATIVE(bl_String_hash);
NATIVE(bl_String_eq);
NATIVE(bl_String_iter);
NATIVE(bl_String_next);
// } String

#endif
