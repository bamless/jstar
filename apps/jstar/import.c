#include "import.h"

#include <stdio.h>
#include <string.h>

#include "dynload.h"
#include "jstar/parse/vector.h"
#include "path.h"
#include "profiler.h"

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

// Vector that keeps track of loaded shared libraries. Used during shutdown to free resources.
static ext_vector(void*) sharedLibs;

// Init the `importPaths` list by appending the script directory (or the current working
// directory if no script was provided) and all the paths present in the JSTARPATH env variable.
// All paths are converted to absolute ones.
static void initImportPaths(JStarVM* vm, const char* scriptPath, bool ignoreEnv) {
    jsrGetGlobal(vm, JSR_CORE_MODULE, IMPORT_PATHS);

    Path mainImport = pathNew();
    if(scriptPath) {
        pathAppendStr(&mainImport, scriptPath);
        pathDirname(&mainImport);
    } else {
        pathAppendStr(&mainImport, "./");
    }

    pathToAbsolute(&mainImport);

    jsrPushString(vm, mainImport.data);
    jsrListAppend(vm, -2);
    jsrPop(vm);

    pathFree(&mainImport);

    // Add all other paths appearing in the JSTARPATH environment variable
    const char* jstarPath;
    if(!ignoreEnv && (jstarPath = getenv(JSTAR_PATH))) {
        Path importPath = pathNew();

        size_t pathLen = strlen(jstarPath);
        for(size_t i = 0, last = 0; i <= pathLen; i++) {
            if(jstarPath[i] == IMPORT_PATHS_SEP || i == pathLen) {
                pathAppend(&importPath, jstarPath + last, i - last);
                pathToAbsolute(&importPath);

                // Add it to the list
                jsrPushString(vm, importPath.data);
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
}

void initImports(JStarVM* vm, const char* scriptPath, bool ignoreEnv) {
    initImportPaths(vm, scriptPath, ignoreEnv);
    import = pathNew();
    nativeExt = pathNew();
    sharedLibs = NULL;
}

void freeImports(void) {
    pathFree(&import);
    pathFree(&nativeExt);
    ext_vec_foreach(void** dynlib, sharedLibs) {
        dynfree(*dynlib);
    }
    ext_vec_free(sharedLibs);
}

// Loads a native extension module and returns its `native registry` to J*
static JStarNativeReg* loadNativeExtension(const Path* modulePath) {
    PROFILE_FUNC()
    pathAppend(&nativeExt, modulePath->data, modulePath->size);
    pathChangeExtension(&nativeExt, DL_SUFFIX);

    void* dynlib;
    {
        PROFILE("loadNativeExtension::dynload")
        dynlib = dynload(nativeExt.data);
        if(!dynlib) {
            return NULL;
        }
    }

    JStarNativeReg* (*registry)(void);
    {
        PROFILE("loadNativeExtension::dynsim")
        registry = dynsim(dynlib, OPEN_NATIVE_EXT);
        if(!registry) {
            dynfree(dynlib);
            return NULL;
        }
    }

    // Track the loaded shared library in the global list of all open shared libraries
    ext_vec_push_back(sharedLibs, dynlib);

    return (*registry)();
}

// Reads a whole file into memory and returns its content and length
static void* readFile(const Path* p, size_t* length) {
    PROFILE_FUNC()
    FILE* f = fopen(p->data, "rb");
    if(!f) {
        return NULL;
    }

    if(fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if(size < 0) {
        fclose(f);
        return NULL;
    }

    *length = size;

    if(fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }

    uint8_t* data = malloc(size);
    if(!data) {
        fclose(f);
        return NULL;
    }

    if(fread(data, 1, size, f) != *length) {
        fclose(f);
        free(data);
        return NULL;
    }

    fclose(f);
    return data;
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
static JStarImportResult createImportResult(char* data, size_t length, const Path* path) {
    PROFILE_FUNC()
    JStarImportResult res;
    res.finalize = &finalizeImport;
    res.code = data;
    res.codeLength = length;
    res.path = path->data;
    res.reg = loadNativeExtension(path);
    res.userData = data;
    return res;
}

JStarImportResult importCallback(JStarVM* vm, const char* moduleName) {
    PROFILE_FUNC()

    // Retrieve the import paths list from the core module
    if(!jsrGetGlobal(vm, JSR_CORE_MODULE, IMPORT_PATHS)) {
        jsrPop(vm);
        return (JStarImportResult){0};
    }

    if(!jsrIsList(vm, -1)) {
        jsrPop(vm);
        return (JStarImportResult){0};
    }

    size_t importLen = jsrListGetLength(vm, -1);

    {
        PROFILE("importCallback::resolutionLoop")

        for(size_t i = 0; i < importLen; i++) {
            jsrListGet(vm, i, -1);
            if(!jsrIsString(vm, -1)) {
                jsrPop(vm);
                continue;
            }

            pathAppend(&import, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
            size_t moduleStart = import.size;

            pathJoinStr(&import, moduleName);
            size_t moduleEnd = import.size;

            pathReplace(&import, moduleStart, '.', PATH_SEP_CHAR);

            char* data;
            size_t length;

            // Try loading a package (__package__ file inside a directory)
            pathJoinStr(&import, PACKAGE_FILE);

            // Try binary package
            pathChangeExtension(&import, JSC_EXT);
            if((data = readFile(&import, &length)) != NULL) {
                jsrPopN(vm, 2);
                return createImportResult(data, length, &import);
            }

            // Try source package
            pathChangeExtension(&import, JSR_EXT);
            if((data = readFile(&import, &length)) != NULL) {
                jsrPopN(vm, 2);
                return createImportResult(data, length, &import);
            }

            // If no package is found, try to load a module
            pathTruncate(&import, moduleEnd);

            // Try binary module
            pathChangeExtension(&import, JSC_EXT);
            if((data = readFile(&import, &length)) != NULL) {
                jsrPopN(vm, 2);
                return createImportResult(data, length, &import);
            }

            // Try source module
            pathChangeExtension(&import, JSR_EXT);
            if((data = readFile(&import, &length)) != NULL) {
                jsrPopN(vm, 2);
                return createImportResult(data, length, &import);
            }

            pathClear(&import);
            jsrPop(vm);
        }
    }

    jsrPop(vm);
    return (JStarImportResult){0};
}
