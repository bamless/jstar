#ifndef CORE_H
#define CORE_H

#include "jstar.h"

// Excepttion class fields
#define EXC_ERR   "_err"
#define EXC_CAUSE "_cause"
#define EXC_TRACE "_stacktrace"

// J* core module bootstrap
void initCoreModule(JStarVM* vm);

// J* core module native functions and methods

// class Number
JSR_NATIVE(jsr_Number_new);
JSR_NATIVE(jsr_Number_isInt);
JSR_NATIVE(jsr_Number_string);
JSR_NATIVE(jsr_Number_hash);
// end

// class Boolean
JSR_NATIVE(jsr_Boolean_new);
JSR_NATIVE(jsr_Boolean_string);
JSR_NATIVE(jsr_Boolean_hash);
// end

// class Null
JSR_NATIVE(jsr_Null_string);
// end

// class Function
JSR_NATIVE(jsr_Function_string);
JSR_NATIVE(jsr_Function_arity);
JSR_NATIVE(jsr_Function_vararg);
JSR_NATIVE(jsr_Function_defaults);
JSR_NATIVE(jsr_Function_getName);
// end

// class Module
JSR_NATIVE(jsr_Module_string);
JSR_NATIVE(jsr_Module_globals);
// end

// class Iterable
JSR_NATIVE(jsr_Iterable_join);
// end

// class List
JSR_NATIVE(jsr_List_new);
JSR_NATIVE(jsr_List_add);
JSR_NATIVE(jsr_List_insert);
JSR_NATIVE(jsr_List_removeAt);
JSR_NATIVE(jsr_List_clear);
JSR_NATIVE(jsr_List_sort);
JSR_NATIVE(jsr_List_len);
JSR_NATIVE(jsr_List_plus);
JSR_NATIVE(jsr_List_eq);
JSR_NATIVE(jsr_List_iter);
JSR_NATIVE(jsr_List_next);
// end

// class Tuple
JSR_NATIVE(jsr_Tuple_new);
JSR_NATIVE(jsr_Tuple_len);
JSR_NATIVE(jsr_Tuple_add);
JSR_NATIVE(jsr_Tuple_eq);
JSR_NATIVE(jsr_Tuple_iter);
JSR_NATIVE(jsr_Tuple_next);
JSR_NATIVE(jsr_Tuple_hash);
// end

// class String
JSR_NATIVE(jsr_String_new);
JSR_NATIVE(jsr_String_charAt);
JSR_NATIVE(jsr_String_startsWith);
JSR_NATIVE(jsr_String_endsWith);
JSR_NATIVE(jsr_String_split);
JSR_NATIVE(jsr_String_strip);
JSR_NATIVE(jsr_String_chomp);
JSR_NATIVE(jsr_String_escaped);
JSR_NATIVE(jsr_String_mul);
JSR_NATIVE(jsr_String_mod);
JSR_NATIVE(jsr_String_len);
JSR_NATIVE(jsr_String_string);
JSR_NATIVE(jsr_String_hash);
JSR_NATIVE(jsr_String_eq);
JSR_NATIVE(jsr_String_iter);
JSR_NATIVE(jsr_String_next);
// end

// class Table
JSR_NATIVE(jsr_Table_new);
JSR_NATIVE(jsr_Table_get);
JSR_NATIVE(jsr_Table_set);
JSR_NATIVE(jsr_Table_len);
JSR_NATIVE(jsr_Table_delete);
JSR_NATIVE(jsr_Table_clear);
JSR_NATIVE(jsr_Table_contains);
JSR_NATIVE(jsr_Table_keys);
JSR_NATIVE(jsr_Table_values);
JSR_NATIVE(jsr_Table_iter);
JSR_NATIVE(jsr_Table_next);
JSR_NATIVE(jsr_Table_string);
// end

// class Enum
JSR_NATIVE(jsr_Enum_new);
JSR_NATIVE(jsr_Enum_value);
JSR_NATIVE(jsr_Enum_name);
// end

// Builtin functions
JSR_NATIVE(jsr_ascii);
JSR_NATIVE(jsr_char);
JSR_NATIVE(jsr_eval);
JSR_NATIVE(jsr_garbageCollect);
JSR_NATIVE(jsr_int);
JSR_NATIVE(jsr_print);
JSR_NATIVE(jsr_type);

// class Exception
JSR_NATIVE(jsr_Exception_printStacktrace);
JSR_NATIVE(jsr_Exception_getStacktrace);
// end

#endif // CORE_H
