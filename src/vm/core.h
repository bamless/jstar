#ifndef CORE_H
#define CORE_H

#include "blang.h"

typedef struct VM VM;

void initCoreLibrary(VM *vm);

NATIVE(bl_int);
NATIVE(bl_num);
NATIVE(bl_list);
NATIVE(bl_isInt);
NATIVE(bl_printstr);

// class Number {
	NATIVE(bl_Number_string);
	NATIVE(bl_Number_class);
	NATIVE(bl_Number_hash);
// } Number

// class Boolean {
	NATIVE(bl_Boolean_string);
	NATIVE(bl_Boolean_class);
// } Boolean

// class Null {
	NATIVE(bl_Null_string);
	NATIVE(bl_Null_class);
// } Null

// class Function {
	NATIVE(bl_Function_string);
// } Function

// class Module {
	NATIVE(bl_Module_string);
// } Module

// class List {
	NATIVE(bl_List_add);
	NATIVE(bl_List_insert);
	NATIVE(bl_List_size);
	NATIVE(bl_List_removeAt);
	NATIVE(bl_List_clear);
// } List

// class String {
	NATIVE(bl_substr);
	NATIVE(bl_String_length);
	NATIVE(bl_String_hash);
// } String

#endif
