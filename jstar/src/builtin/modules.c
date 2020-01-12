#include "modules.h"
#include "object.h"

#include "core.jsr.h"
#include "core.h"

#include "sys.jsr.h"
#include "sys.h"

#include "io.jsr.h"
#include "io.h"

#include "math.jsr.h"
#include "math.h"

#include "debug.jsr.h"
#include "debug.h"

#include "re.jsr.h"
#include "re.h"

#include <string.h>

typedef enum { TYPE_FUNC, TYPE_CLASS } Type;

typedef struct {
    const char *name;
    JStarNative func;
} Func;

typedef struct {
    const char *name;
    Func methods[16];
} Class;

typedef struct {
    Type type;
    union {
        Func function;
        Class class;
    };
} ModuleElem;

typedef struct {
    const char *name;
    const char **src;
    ModuleElem elems[31];
} Module;

// clang-format off

#define ELEMS_END        {TYPE_FUNC, .function = METHODS_END},
#define MODULES_END      {NULL, NULL, { ELEMS_END }}
#define METHODS_END      {NULL, NULL}

#define MODULE(name)     { #name, &name##_jsr, {
#define ENDMODULE        ELEMS_END } },

#define COREMODULE       {"__core__", &core_jsr, {

#define CLASS(name)      { TYPE_CLASS, .class = { #name, {
#define METHOD(name, fn) { #name, fn },
#define ENDCLASS         METHODS_END } } },

#define FUNCTION(name, fn) { TYPE_FUNC, .function = { #name, fn } },

Module builtInModules[] = {
    COREMODULE
        FUNCTION(ascii,  jsr_ascii)
        FUNCTION(char,   jsr_char)
        FUNCTION(eval,   jsr_eval)
        FUNCTION(exec,   jsr_exec)
        FUNCTION(int,    jsr_int)
        FUNCTION(print,  jsr_print)
        FUNCTION(system, jsr_system)
        FUNCTION(type,   jsr_type)

        CLASS(Number)
            METHOD(new,        jsr_Number_new)
            METHOD(isInt,      jsr_Number_isInt)
            METHOD(__string__, jsr_Number_string)
            METHOD(__hash__,   jsr_Number_hash)
        ENDCLASS

        CLASS(Boolean)
            METHOD(new,        jsr_Boolean_new)
            METHOD(__string__, jsr_Boolean_string)
        ENDCLASS

        CLASS(Null)
            METHOD(__string__, jsr_Null_string)
        ENDCLASS

        CLASS(Function)
            METHOD(__string__, jsr_Function_string)
        ENDCLASS

        CLASS(Module)
            METHOD(__string__, jsr_Module_string)
        ENDCLASS

        CLASS(List)
            METHOD(new,      jsr_List_new)
            METHOD(add,      jsr_List_add)
            METHOD(insert,   jsr_List_insert)
            METHOD(removeAt, jsr_List_removeAt)
            METHOD(clear,    jsr_List_clear)
            METHOD(subList,  jsr_List_subList)
            METHOD(__len__,  jsr_List_len)
            METHOD(__iter__, jsr_List_iter)
            METHOD(__next__, jsr_List_next)
        ENDCLASS

        CLASS(Tuple)
            METHOD(new,      jsr_Tuple_new)
            METHOD(sub,      jsr_Tuple_sub)
            METHOD(__len__,  jsr_Tuple_len)
            METHOD(__iter__, jsr_Tuple_iter)
            METHOD(__next__, jsr_Tuple_next)
        ENDCLASS

        CLASS(String)
            METHOD(new,        jsr_String_new)
            METHOD(substr,     jsr_String_substr)
            METHOD(startsWith, jsr_String_startsWith)
            METHOD(endsWith,   jsr_String_endsWith)
            METHOD(strip,      jsr_String_strip)
            METHOD(chomp,      jsr_String_chomp)
            METHOD(join,       jsr_String_join)
            METHOD(__eq__,     jsr_String_eq)
            METHOD(__len__,    jsr_String_len)
            METHOD(__hash__,   jsr_String_hash)
            METHOD(__iter__,   jsr_String_iter)
            METHOD(__next__,   jsr_String_next)
            METHOD(__string__, jsr_String_string)
        ENDCLASS

        CLASS(Table)
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
        ENDCLASS
    ENDMODULE
    MODULE(sys)
        FUNCTION(time,           jsr_time)
        FUNCTION(exit,           jsr_exit)
        FUNCTION(getImportPaths, jsr_getImportPaths)
        FUNCTION(platform,       jsr_platform)
        FUNCTION(clock,          jsr_clock)
        FUNCTION(gc,             jsr_gc)
        FUNCTION(init,           jsr_sys_init)
    ENDMODULE
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

        CLASS(__PFile)
            METHOD(close, jsr_PFile_close)
        ENDCLASS

        FUNCTION(popen,  jsr_popen)
        FUNCTION(remove, jsr_remove)
        FUNCTION(rename, jsr_rename)
    ENDMODULE
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
    MODULE(re)
        FUNCTION(match,  jsr_re_match)
        FUNCTION(find,   jsr_re_find)
        FUNCTION(gmatch, jsr_re_gmatch)
        FUNCTION(gsub,   jsr_re_gsub)
    ENDMODULE
    MODULE(debug)
        FUNCTION(printStack,  jsr_printStack)
        FUNCTION(disassemble, jsr_disassemble)
    ENDMODULE
    MODULES_END
};

// clang-format on

static Module *getModule(const char *name) {
    for(int i = 0; builtInModules[i].name != NULL; i++) {
        if(strcmp(name, builtInModules[i].name) == 0) {
            return &builtInModules[i];
        }
    }
    return NULL;
}

static Class *getClass(Module *module, const char *name) {
    for(int i = 0;; i++) {
        ModuleElem *e = &module->elems[i];
        if(e->type == TYPE_FUNC && e->function.name == NULL) return NULL;

        if(e->type == TYPE_CLASS) {
            if(strcmp(module->elems[i].class.name, name) == 0) {
                return &module->elems[i].class;
            }
        }
    }
}

static JStarNative getNativeMethod(Class *cls, const char *name) {
    for(int i = 0; cls->methods[i].name != NULL; i++) {
        if(strcmp(cls->methods[i].name, name) == 0) {
            return cls->methods[i].func;
        }
    }
    return NULL;
}

static JStarNative getNativeFunc(Module *module, const char *name) {
    for(int i = 0;; i++) {
        if(module->elems[i].type == TYPE_FUNC) {
            if(module->elems[i].function.name == NULL) return NULL;

            if(strcmp(module->elems[i].function.name, name) == 0) {
                return module->elems[i].function.func;
            }
        }
    }
}

JStarNative resolveBuiltIn(const char *module, const char *cls, const char *name) {
    Module *m = getModule(module);
    if(m == NULL) return NULL;

    if(cls == NULL) {
        return getNativeFunc(m, name);
    }

    Class *c = getClass(m, cls);
    if(c == NULL) return NULL;

    return getNativeMethod(c, name);
}

const char *readBuiltInModule(const char *name) {
    Module *m = getModule(name);
    if(m != NULL) {
        return *m->src;
    }
    return NULL;
}
