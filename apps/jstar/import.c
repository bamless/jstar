#include "import.h"

#include <extlib.h>
#include <jstar/jstar.h>
#include <path.h>
#include <stdio.h>
#include <string.h>

#include "dynload.h"

#define PACKAGE_FILE    "__package__"    // Name of the file executed during package imports
#define JSR_EXT         ".jsr"           // Normal J* source file extension
#define JSC_EXT         ".jsc"           // Compiled J* file extension
#define JSTAR_PATH      "JSTARPATH"      // Env variable containing a list of import paths
#define IMPORT_PATHS    "importPaths"    // Name of the global holding the import paths list
#define OPEN_NATIVE_EXT "jsrOpenModule"  // Function called when loading native extension modules

// Platform specific separator for the `JSTARPATH` environment variable
#ifdef JSTAR_WINDOWS
    #define IMPORT_PATHS_SEP ';'
#else
    #define IMPORT_PATHS_SEP ':'
#endif

// Platform specific shared library suffix
#if defined(JSTAR_WINDOWS)
    #define DL_SUFFIX ".dll"
#elif defined(JSTAR_MACOS) || defined(JSTAR_IOS)
    #define DL_SUFFIX ".dylib"
#elif defined(JSTAR_POSIX)
    #define DL_SUFFIX ".so"
#else
    #define DL_SUFFIX ""
#endif

// Static global `Path`s used to construct the final import paths.
// Since imports are always sequential (and thus we need not to worry about concurrency),
// having them as globals saves a lot of allocations during imports.
static Path import;
static Path nativeExt;

bool initImports(JStarVM* vm, const char* scriptPath, bool ignoreEnv) {
    jsrGetGlobal(vm, JSR_CORE_MODULE, IMPORT_PATHS);

    Path mainImport;
    if(scriptPath) {
        mainImport = pathNew(scriptPath);
        pathDirname(&mainImport);
    } else {
        mainImport = pathNew("./");
    }
    if(!pathToAbsolute(&mainImport)) return false;

    jsrPushString(vm, mainImport.items);
    jsrListAppend(vm, -2);
    jsrPop(vm);

    pathFree(&mainImport);

    // Add all other paths appearing in the JSTARPATH environment variable
    const char* jstarPath;
    if(!ignoreEnv && (jstarPath = getenv(JSTAR_PATH))) {
        Path importPath = {0};

        size_t pathLen = strlen(jstarPath);
        for(size_t i = 0, last = 0; i <= pathLen; i++) {
            if(jstarPath[i] == IMPORT_PATHS_SEP || i == pathLen) {
                pathAppend(&importPath, jstarPath + last, i - last);
                if(!pathToAbsolute(&importPath)) return false;

                // Add it to the list
                jsrPushString(vm, importPath.items);
                jsrListAppend(vm, -2);
                jsrPop(vm);

                pathClear(&importPath);
                last = i + 1;
            }
        }

        pathFree(&importPath);
    }

    // Add the CWD (`./`) as a last importPath
    jsrPushString(vm, "./");
    jsrListAppend(vm, -2);
    jsrPop(vm);

    jsrPop(vm);
    return true;
}

void freeImports(void) {
    pathFree(&import);
    pathFree(&nativeExt);
}

// Loads a native extension module and returns its `native registry` to J*
static JStarNativeReg* loadNativeExtension(const Path* modulePath) {
    pathAppend(&nativeExt, modulePath->items, modulePath->size);
    pathChangeExtension(&nativeExt, DL_SUFFIX);

    void* dynlib;
    {
        dynlib = dynload(nativeExt.items);
        if(!dynlib) {
            return NULL;
        }
    }

    JStarNativeReg* (*registry)(void);
    {
        registry = dynsim(dynlib, OPEN_NATIVE_EXT);
        if(!registry) {
            dynfree(dynlib);
            return NULL;
        }
    }

    return (*registry)();
}

static bool readFileAtPath(const Path* p, StringBuffer* sb) {
    Context ctx = *ext_context;
    ctx.log_level = NO_LOGGING;
    push_context(&ctx);
    bool res = read_file(p->items, sb);
    pop_context();
    return res;
}

// Callback called by J* when an import statement is finished.
// Used to reset global state and free the previously read code.
static void finalizeImport(void* userData) {
    pathClear(&import);
    pathClear(&nativeExt);
    char* data = userData;
    free(data);
}

// Creates a `JStarImportResult` and sets all relevant fields such as
// the finalization callback and the native registry structure
static JStarImportResult makeImportResult(const Path* path, StringBuffer sb) {
    JStarImportResult res;
    res.finalize = &finalizeImport;
    res.code = sb.items ? sb.items : "";
    res.codeLength = sb.size;
    res.path = path->items;
    res.reg = loadNativeExtension(path);
    res.userData = sb.items;
    return res;
}

JStarImportResult importCallback(JStarVM* vm, const char* moduleName) {
    // Retrieve the import paths list from the core module
    if(!jsrGetGlobal(vm, JSR_CORE_MODULE, IMPORT_PATHS)) {
        jsrPop(vm);
        return (JStarImportResult){0};
    }

    // If its not a list fail
    if(!jsrIsList(vm, -1)) {
        jsrPop(vm);
        return (JStarImportResult){0};
    }

    size_t importLen = jsrListGetLength(vm, -1);
    for(size_t i = 0; i < importLen; i++) {
        jsrListGet(vm, i, -1);
        if(!jsrIsString(vm, -1)) {
            jsrPop(vm);
            continue;
        }

        pathAppend(&import, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
        size_t moduleStart = import.size - 1;

        pathJoinStr(&import, moduleName);
        size_t moduleEnd = import.size - 1;

        pathReplace(&import, moduleStart, ".", PATH_SEP_CHAR);

        StringBuffer sb = {.allocator = &default_allocator.base};

        // Try loading a package (__package__ file inside a directory)
        pathJoinStr(&import, PACKAGE_FILE);

        // Try binary package
        pathChangeExtension(&import, JSC_EXT);
        if(readFileAtPath(&import, &sb)) {
            return (jsrPopN(vm, 2), makeImportResult(&import, sb));
        }

        // Try source package
        pathChangeExtension(&import, JSR_EXT);
        if(readFileAtPath(&import, &sb)) {
            return (jsrPopN(vm, 2), makeImportResult(&import, sb));
        }

        // If no package is found, try to load a module
        pathTruncate(&import, moduleEnd);

        // Try binary module
        pathChangeExtension(&import, JSC_EXT);
        if(readFileAtPath(&import, &sb)) {
            return (jsrPopN(vm, 2), makeImportResult(&import, sb));
        }

        // Try source module
        pathChangeExtension(&import, JSR_EXT);
        if(readFileAtPath(&import, &sb)) {
            return (jsrPopN(vm, 2), makeImportResult(&import, sb));
        }

        pathClear(&import);
        jsrPop(vm);
    }

    jsrPop(vm);
    return (JStarImportResult){0};
}
