#ifndef CORE_H
#define CORE_H

#include "jstar.h"

// J* core libray bootstrap
void initCoreLibrary(JStarVM *vm);

JSR_NATIVE(jsr_int);
JSR_NATIVE(jsr_num);
JSR_NATIVE(jsr_isInt);

JSR_NATIVE(jsr_char);
JSR_NATIVE(jsr_ascii);

JSR_NATIVE(jsr_print);
JSR_NATIVE(jsr_eval);

JSR_NATIVE(jsr_type);

// class Number {
JSR_NATIVE(jsr_Number_string);
JSR_NATIVE(jsr_Number_hash);
// } Number

// class Boolean {
JSR_NATIVE(jsr_Boolean_string);
// } Boolean

// class Null {
JSR_NATIVE(jsr_Null_string);
// } Null

// class Function {
JSR_NATIVE(jsr_Function_string);
// } Function

// class Module {
JSR_NATIVE(jsr_Module_string);
// } Module

// class List {
JSR_NATIVE(jsr_List_new);
JSR_NATIVE(jsr_List_add);
JSR_NATIVE(jsr_List_insert);
JSR_NATIVE(jsr_List_len);
JSR_NATIVE(jsr_List_removeAt);
JSR_NATIVE(jsr_List_clear);
JSR_NATIVE(jsr_List_subList);
JSR_NATIVE(jsr_List_iter);
JSR_NATIVE(jsr_List_next);
// } List

// class Tuple {
JSR_NATIVE(jsr_Tuple_new);
JSR_NATIVE(jsr_Tuple_len);
JSR_NATIVE(jsr_Tuple_iter);
JSR_NATIVE(jsr_Tuple_next);
JSR_NATIVE(jsr_Tuple_sub);
// }

// class String {
JSR_NATIVE(jsr_substr);
JSR_NATIVE(jsr_String_len);
JSR_NATIVE(jsr_String_string);
JSR_NATIVE(jsr_String_join);
JSR_NATIVE(jsr_String_hash);
JSR_NATIVE(jsr_String_eq);
JSR_NATIVE(jsr_String_iter);
JSR_NATIVE(jsr_String_next);
// } String

#endif
