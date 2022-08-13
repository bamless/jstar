#include "import.h"

#include <cwalk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "console_print.h"
#include "dynload.h"
#include "jstar/parse/vector.h"

#define JSTAR_PATH   "JSTARPATH"      // Env variable containing a list of import paths
#define IMPORT_PATHS "importPaths"    // Name of the global holding the import paths list in the core module
#define OPEN_NATIVE  "jsrOpenModule"  // Function called when loading native extension modules
#define PACKAGE_FILE "__package__"    // Name of the file executed during package imports
#define JSR_EXT      ".jsr"           // Normal J* source file extension
#define JSC_EXT      ".jsc"           // Compiled J* file extension

// Platform specific separator for the `JSTARPATH` environment variable
#if defined(JSTAR_POSIX)
    #include <unistd.h>
    #define IMPORT_PATHS_SEP ':'
#elif defined(JSTAR_WINDOWS)
    #include <direct.h>
    #define getcwd          _getcwd
    #define IMPORT_PATHS_SEP ';'
#endif

// Platform specific path separator
#ifdef JSTAR_WINDOWS
    #define PATH_SEP_CHAR '\\'
    #define PATH_SEP_STR  "\\"
#else
    #define PATH_SEP_CHAR '/'
    #define PATH_SEP_STR  "/"
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

// Static global buffers used to construct the import paths.
// Since imports are always sequential (and thus we need not to worry about concurrency),
// having them as globals saves a lot of allocations during imports.
static JStarBuffer importBuilder;
static JStarBuffer nativeExtBuilder;

// Vector that keeps track of loaded shared libraries. Used during shutdown to free resources.
static Vector sharedLibs;

// Returns the current working directory.
// The returned buffer is malloc'd and should be freed by the user.
static char* getCurrentDirectory(void) {
    size_t cwdLen = 128;
    char* cwd = malloc(cwdLen);
    while(!getcwd(cwd, cwdLen)) {
        if(errno != ERANGE) {
            int saveErrno = errno;
            free(cwd);
            errno = saveErrno;
            return NULL;
        }
        cwdLen *= 2;
        cwd = realloc(cwd, cwdLen);
    }
    return cwd;
}

// Init the J* `importPaths` list by appending the script directory (or the current working
// directory if no script was provided) and all the paths present in the JSTARPATH env variable.
// All paths are converted to absolute ones.
static void initImportPaths(JStarVM* vm, const char* scriptPath, bool ignoreEnv) {
    char absolutePath[FILENAME_MAX];

    // Create the import paths list inside the `core` module
    jsrPushList(vm);
    jsrSetGlobal(vm, JSR_CORE_MODULE, "importPaths");

    // Compute and add the absolute main import path to the import paths list
    char* currentDir = getCurrentDirectory();
    if(currentDir) {
        char* mainImportPath;
    
        if(scriptPath) {
            size_t directory = 0;
            cwk_path_get_dirname(scriptPath, &directory);
            mainImportPath = calloc(directory + 1, 1);
            memcpy(mainImportPath, scriptPath, directory);
        } else {
            mainImportPath = calloc(strlen("./") + 1, 1);
            memcpy(mainImportPath, "./", 2);
        }

        cwk_path_get_absolute(currentDir, mainImportPath, absolutePath, FILENAME_MAX);

        // Add it to the list
        jsrPushString(vm, absolutePath);
        jsrListAppend(vm, -2);
        jsrPop(vm);

        free(mainImportPath);
    }

    // Add all other paths appearing in the JSTARPATH environment variable
    const char* jstarPath;
    if(!ignoreEnv && (jstarPath = getenv(JSTAR_PATH))) {
        JStarBuffer builder;
        jsrBufferInit(vm, &builder);

        size_t pathLen = strlen(jstarPath);
        for(size_t i = 0, last = 0; i <= pathLen; i++) {
            if(jstarPath[i] == IMPORT_PATHS_SEP || i == pathLen) {
                jsrBufferAppend(&builder, jstarPath + last, i - last);
                cwk_path_get_absolute(currentDir, builder.data, absolutePath, FILENAME_MAX);

                // Add it to the list
                jsrPushString(vm, absolutePath);
                jsrListAppend(vm, -2);
                jsrPop(vm);

                jsrBufferClear(&builder);
                last = i + 1;
            }
        }

        jsrBufferFree(&builder);
    }

    // Add the CWD (`./`) as a last importPath
    jsrPushString(vm, "./");
    jsrListAppend(vm, -2);
    jsrPop(vm);

    free(currentDir);
    jsrPop(vm);
}

void initImports(JStarVM* vm, const char* scriptPath, bool ignoreEnv) {
    initImportPaths(vm, scriptPath, ignoreEnv);
    jsrBufferInit(vm, &importBuilder);
    jsrBufferInit(vm, &nativeExtBuilder);
    sharedLibs = vecNew();
}

void freeImports(void) {
    jsrBufferFree(&importBuilder);
    jsrBufferFree(&nativeExtBuilder);
    // Free all loaded share libraries (if any)
    vecForeach(void** dynlib, sharedLibs) {
        dynfree(*dynlib);
    }
    vecFree(&sharedLibs);
}

static JStarNativeReg* loadNativeExtension(const char* modulePath) {
    jsrBufferAppendStr(&nativeExtBuilder, modulePath);

    size_t ext = strrchr(modulePath, '.') - modulePath;
    jsrBufferTrunc(&nativeExtBuilder, ext);
    jsrBufferAppendStr(&nativeExtBuilder, DL_SUFFIX);

    void* dynlib = dynload(nativeExtBuilder.data);
    if(!dynlib) {
        return NULL;
    }

    JStarNativeReg* (*registry)(void) = dynsim(dynlib, OPEN_NATIVE);
    if(!registry) {
        dynfree(dynlib);
        return NULL;
    }

    // Track the loaded shared library in the global list of all open shared libraries
    vecPush(&sharedLibs, dynlib);

    return (*registry)();
}

static void finalizeImport(void* userData) {
    jsrBufferClear(&importBuilder);
    jsrBufferClear(&nativeExtBuilder);

    JStarBuffer* code = userData;
    jsrBufferFree(code);
    free(code);
}

static JStarImportResult createImportResult(JStarBuffer* code, const char* path) {
    JStarImportResult res;
    res.finalize = &finalizeImport;
    res.code = code->data;
    res.codeLength = code->size;
    res.path = path;
    res.reg = loadNativeExtension(path);
    res.userData = code;
    return res;
}

JStarImportResult importCallback(JStarVM* vm, const char* moduleName) {
    size_t moduleNameLen = strlen(moduleName);

    // Retrieve the import paths list from the core module
    jsrGetGlobal(vm, JSR_CORE_MODULE, IMPORT_PATHS);
    if(!jsrIsList(vm, -1)) {
        jsrPop(vm);
        return (JStarImportResult){0};
    }


    bool err;
    jsrPushNull(vm);
    JStarBuffer* code = malloc(sizeof(JStarBuffer));

    // Iterate the import paths list
    while(jsrIter(vm, -2, -1, &err)) {
        if(err || !jsrNext(vm, -2, -1)) {
            jsrPop(vm);
            break;
        }

        if(!jsrIsString(vm, -1)) {
            jsrPop(vm);
            continue;
        }

        jsrBufferAppend(&importBuilder, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
        // Correct for separator at the end of the import path
        if(importBuilder.size > 0 && importBuilder.data[importBuilder.size - 1] != PATH_SEP_CHAR) {
            jsrBufferAppendChar(&importBuilder, PATH_SEP_CHAR);
        }

        size_t moduleStart = importBuilder.size;
        size_t moduleEnd = moduleStart + moduleNameLen;
        jsrBufferAppendStr(&importBuilder, moduleName);
        jsrBufferReplaceChar(&importBuilder, moduleStart, '.', PATH_SEP_CHAR);

        // Try to load a binary package (__package__.jsc file in a directory)
        jsrBufferAppendStr(&importBuilder, PATH_SEP_STR PACKAGE_FILE JSC_EXT);
        if(jsrReadFile(vm, importBuilder.data, code)) {
            jsrPopN(vm, 3);
            return createImportResult(code, importBuilder.data);
        }

        // Try to load a source package (__package__.jsr file in a directory)
        jsrBufferTrunc(&importBuilder, moduleEnd);
        jsrBufferAppendStr(&importBuilder, PATH_SEP_STR PACKAGE_FILE JSR_EXT);
        if(jsrReadFile(vm, importBuilder.data, code)) {
            jsrPopN(vm, 3);
            return createImportResult(code, importBuilder.data);
        }

        // If there is no package try to load compiled module (i.e. `.jsc` file)
        jsrBufferTrunc(&importBuilder, moduleEnd);
        jsrBufferAppendStr(&importBuilder, JSC_EXT);
        if(jsrReadFile(vm, importBuilder.data, code)) {
            jsrPopN(vm, 3);
            return createImportResult(code, importBuilder.data);
        }

        // No binary module found, finally try with source module (i.e. `.jsr` file)
        jsrBufferTrunc(&importBuilder, moduleEnd);
        jsrBufferAppendStr(&importBuilder, JSR_EXT);
        if(jsrReadFile(vm, importBuilder.data, code)) {
            jsrPopN(vm, 3);
            return createImportResult(code, importBuilder.data);
        }

        jsrBufferClear(&importBuilder);
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
    free(code);

    return (JStarImportResult){0};
}