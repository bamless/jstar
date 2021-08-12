#include "modules.h"

#include "core.h"
#include "core.jsc.inc"

#ifdef JSTAR_SYS
    #include "sys.h"
    #include "sys.jsc.inc"
#endif

#ifdef JSTAR_IO
    #include "io.h"
    #include "io.jsc.inc"
#endif

#ifdef JSTAR_MATH
    #include "math.h"
    #include "math.jsc.inc"
#endif

#ifdef JSTAR_DEBUG
    #include "debug.h"
    #include "debug.jsc.inc"
#endif

#ifdef JSTAR_RE
    #include "re.h"
    #include "re.jsc.inc"
#endif

#include <string.h>

typedef enum { TYPE_FUNC, TYPE_CLASS } Type;

typedef struct {
    const char* name;
    JStarNative func;
} Func;

typedef struct {
    const char* name;
    Func methods[17];
} Class;

typedef struct {
    Type type;
    union {
        Func function;
        Class class;
    } as;
} ModuleElem;

typedef struct {
    const char* name;
    const char** bytecode;
    const size_t* len;
    ModuleElem elems[31];
} Module;

// clang-format off

#define ELEMS_END        {TYPE_FUNC, .as = { .function = METHODS_END } },
#define MODULES_END      {NULL, NULL, 0, { ELEMS_END }}
#define METHODS_END      {NULL, NULL}

#define MODULE(name)     { #name, &name##_jsc, &name##_jsc_len, {
#define ENDMODULE        ELEMS_END } },

#define COREMODULE       {"__core__", &core_jsc, &core_jsc_len, {

#define CLASS(name)      { TYPE_CLASS, .as = { .class = { #name, {
#define METHOD(name, fn) { #name, fn },
#define ENDCLASS         METHODS_END } } } },

#define FUNCTION(name, fn) { TYPE_FUNC, .as = { .function = { #name, fn } } },

static Module builtInModules[] = {
    COREMODULE
        FUNCTION(ascii,          jsr_ascii)
        FUNCTION(char,           jsr_char)
        FUNCTION(eval,           jsr_eval)
        FUNCTION(int,            jsr_int)
        FUNCTION(print,          jsr_print)
        FUNCTION(type,           jsr_type)
        FUNCTION(garbageCollect, jsr_garbageCollect)
        CLASS(Number)
            METHOD(new,        jsr_Number_new)
            METHOD(isInt,      jsr_Number_isInt)
            METHOD(__string__, jsr_Number_string)
            METHOD(__hash__,   jsr_Number_hash)
        ENDCLASS
        CLASS(Boolean)
            METHOD(new,        jsr_Boolean_new)
            METHOD(__string__, jsr_Boolean_string)
            METHOD(__hash__,   jsr_Boolean_hash)
        ENDCLASS
        CLASS(Null)
            METHOD(__string__, jsr_Null_string)
        ENDCLASS
        CLASS(Function)
            METHOD(__string__, jsr_Function_string)
        ENDCLASS
        CLASS(Module)
            METHOD(__string__, jsr_Module_string)
            METHOD(globals,    jsr_Module_globals)
        ENDCLASS
        CLASS(Iterable)
            METHOD(join,       jsr_Iterable_join)
        ENDCLASS
        CLASS(List)
            METHOD(new,      jsr_List_new)
            METHOD(add,      jsr_List_add)
            METHOD(insert,   jsr_List_insert)
            METHOD(removeAt, jsr_List_removeAt)
            METHOD(clear,    jsr_List_clear)
            METHOD(sort,     jsr_List_sort)
            METHOD(__len__,  jsr_List_len)
            METHOD(__add__,  jsr_List_plus)
            METHOD(__eq__,   jsr_List_eq)
            METHOD(__iter__, jsr_List_iter)
            METHOD(__next__, jsr_List_next)
        ENDCLASS
        CLASS(Tuple)
            METHOD(new,      jsr_Tuple_new)
            METHOD(__len__,  jsr_Tuple_len)
            METHOD(__add__,  jsr_Tuple_add)
            METHOD(__eq__,   jsr_Tuple_eq)
            METHOD(__iter__, jsr_Tuple_iter)
            METHOD(__next__, jsr_Tuple_next)
            METHOD(__hash__, jsr_Tuple_hash)
        ENDCLASS
        CLASS(String)
            METHOD(new,        jsr_String_new)
            METHOD(charAt,     jsr_String_charAt)
            METHOD(startsWith, jsr_String_startsWith)
            METHOD(endsWith,   jsr_String_endsWith)
            METHOD(split,      jsr_String_split)
            METHOD(strip,      jsr_String_strip)
            METHOD(chomp,      jsr_String_chomp)
            METHOD(escaped,    jsr_String_escaped)
            METHOD(__mul__,    jsr_String_mul)
            METHOD(__mod__,    jsr_String_mod)
            METHOD(__eq__,     jsr_String_eq)
            METHOD(__len__,    jsr_String_len)
            METHOD(__hash__,   jsr_String_hash)
            METHOD(__iter__,   jsr_String_iter)
            METHOD(__next__,   jsr_String_next)
            METHOD(__string__, jsr_String_string)
        ENDCLASS
        CLASS(Table)
            METHOD(new,        jsr_Table_new)
            METHOD(__get__,    jsr_Table_get)
            METHOD(__set__,    jsr_Table_set)
            METHOD(__len__,    jsr_Table_len)
            METHOD(delete,     jsr_Table_delete)
            METHOD(clear,      jsr_Table_clear)
            METHOD(contains,   jsr_Table_contains)
            METHOD(keys,       jsr_Table_keys)
            METHOD(values,     jsr_Table_values)
            METHOD(__iter__,   jsr_Table_iter)
            METHOD(__next__,   jsr_Table_next)
            METHOD(__string__, jsr_Table_string)
        ENDCLASS
        CLASS(Enum)
            METHOD(new,   jsr_Enum_new)
            METHOD(value, jsr_Enum_value)
            METHOD(name,  jsr_Enum_name)
        ENDCLASS
        CLASS(Exception)
            METHOD(printStacktrace, jsr_Exception_printStacktrace)
            METHOD(getStacktrace,   jsr_Exception_getStacktrace)
        ENDCLASS
    ENDMODULE
#ifdef JSTAR_SYS
    MODULE(sys)
        FUNCTION(time,     jsr_time)
        FUNCTION(exec,     jsr_exec)
        FUNCTION(exit,     jsr_exit)
        FUNCTION(platform, jsr_platform)
        FUNCTION(clock,    jsr_clock)
        FUNCTION(getenv,   jsr_getenv)
        FUNCTION(system,   jsr_system)
        FUNCTION(isPosix,  jsr_isPosix)
    ENDMODULE
#endif
#ifdef JSTAR_IO
    MODULE(io)
        CLASS(File)
            METHOD(new,      jsr_File_new)
            METHOD(read,     jsr_File_read)
            METHOD(readAll,  jsr_File_readAll)
            METHOD(readLine, jsr_File_readLine)
            METHOD(write,    jsr_File_write)
            METHOD(close,    jsr_File_close)
            METHOD(seek,     jsr_File_seek)
            METHOD(tell,     jsr_File_tell)
            METHOD(rewind,   jsr_File_rewind)
            METHOD(flush,    jsr_File_flush)
        ENDCLASS
        CLASS(Popen)
            METHOD(new,   jsr_Popen_new)
            METHOD(close, jsr_Popen_close)
        ENDCLASS
        FUNCTION(remove, jsr_remove)
        FUNCTION(rename, jsr_rename)
        FUNCTION(init,   jsr_io_init)
    ENDMODULE
#endif
#ifdef JSTAR_MATH
    MODULE(math)
        FUNCTION(abs,    jsr_abs)
        FUNCTION(acos,   jsr_acos)
        FUNCTION(asin,   jsr_asin)
        FUNCTION(atan,   jsr_atan)
        FUNCTION(atan2,  jsr_atan2)
        FUNCTION(ceil,   jsr_ceil)
        FUNCTION(cos,    jsr_cos)
        FUNCTION(cosh,   jsr_cosh)
        FUNCTION(deg,    jsr_deg)
        FUNCTION(exp,    jsr_exp)
        FUNCTION(floor,  jsr_floor)
        FUNCTION(frexp,  jsr_frexp)
        FUNCTION(ldexp,  jsr_ldexp)
        FUNCTION(log,    jsr_log)
        FUNCTION(log10,  jsr_log10)
        FUNCTION(max,    jsr_max)
        FUNCTION(min,    jsr_min)
        FUNCTION(rad,    jsr_rad)
        FUNCTION(sin,    jsr_sin)
        FUNCTION(sinh,   jsr_sinh)
        FUNCTION(sqrt,   jsr_sqrt)
        FUNCTION(tan,    jsr_tan)
        FUNCTION(tanh,   jsr_tanh)
        FUNCTION(modf,   jsr_modf)
        FUNCTION(random, jsr_random)
        FUNCTION(seed,   jsr_seed)
        FUNCTION(init,   jsr_math_init)
    ENDMODULE
#endif
#ifdef JSTAR_RE
    MODULE(re)
        FUNCTION(match,  jsr_re_match)
        FUNCTION(find,   jsr_re_find)
        FUNCTION(gmatch, jsr_re_gmatch)
        FUNCTION(gsub,   jsr_re_gsub)
    ENDMODULE
#endif
#ifdef JSTAR_DEBUG
    MODULE(debug)
        FUNCTION(printStack,  jsr_printStack)
        FUNCTION(disassemble, jsr_disassemble)
    ENDMODULE
#endif
    MODULES_END
};

// clang-format on

static Module* getModule(const char* name) {
    for(int i = 0; builtInModules[i].name != NULL; i++) {
        if(strcmp(name, builtInModules[i].name) == 0) {
            return &builtInModules[i];
        }
    }
    return NULL;
}

static Class* getClass(Module* module, const char* name) {
    for(int i = 0;; i++) {
        ModuleElem* e = &module->elems[i];
        if(e->type == TYPE_FUNC && e->as.function.name == NULL) return NULL;

        if(e->type == TYPE_CLASS) {
            if(strcmp(module->elems[i].as.class.name, name) == 0) {
                return &module->elems[i].as.class;
            }
        }
    }
}

static JStarNative getNativeMethod(Class* cls, const char* name) {
    for(int i = 0; cls->methods[i].name != NULL; i++) {
        if(strcmp(cls->methods[i].name, name) == 0) {
            return cls->methods[i].func;
        }
    }
    return NULL;
}

static JStarNative getNativeFunc(Module* module, const char* name) {
    for(int i = 0;; i++) {
        if(module->elems[i].type == TYPE_FUNC) {
            if(module->elems[i].as.function.name == NULL) return NULL;

            if(strcmp(module->elems[i].as.function.name, name) == 0) {
                return module->elems[i].as.function.func;
            }
        }
    }
}

JStarNative resolveBuiltIn(const char* module, const char* cls, const char* name) {
    Module* m = getModule(module);
    if(m == NULL) return NULL;

    if(cls == NULL) {
        return getNativeFunc(m, name);
    }

    Class* c = getClass(m, cls);
    if(c == NULL) return NULL;

    return getNativeMethod(c, name);
}

const char* readBuiltInModule(const char* name, size_t* len) {
    Module* m = getModule(name);
    if(m != NULL) {
        *len = *m->len;
        return *m->bytecode;
    }
    *len = 0;
    return NULL;
}
