#include "dynload.h"

#if defined(JSTAR_POSIX)
    #include <dlfcn.h>
#elif defined(JSTAR_WINDOWS)
    #include <Windows.h>
#else
    #include <stdlib.h>
#endif

void* dynload(const char* path) {
#if defined(JSTAR_POSIX)
    return dlopen(path, RTLD_NOW);
#elif defined(JSTAR_WINDOWS)
    return LoadLibrary(path);
#else
    // Dynamic library loading not supported
    return NULL;
#endif
}

void dynfree(void* handle) {
#if defined(JSTAR_POSIX)
    dlclose(handle);
#elif defined(JSTAR_WINDOWS)
    FreeLibrary(handle);
#else
    // Dynamic library loading not supported
#endif
}

void* dynsim(void* handle, const char* symbol) {
#if defined(JSTAR_POSIX)
    return dlsym(handle, symbol);
#elif defined(JSTAR_WINDOWS)
    return GetProcAddress(handle, symbol);
#else
    // Dynamic library loading not supported
    return NULL;
#endif
}