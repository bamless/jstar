#include "dynload.h"

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#    include <dlfcn.h>
#elif defined(_WIN32)
#    include <Windows.h>
#endif

void *dynload(const char *path) {
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    return dlopen(path, RTLD_NOW);
#elif defined(_WIN32)
    return LoadLibrary(path);
#else
    // Dynamic library loading not supported
    return NULL;
#endif
}

void dynfree(void *handle) {
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    dlclose(handle);
#elif defined(_WIN32)
    FreeLibrary(handle);
#else
    // Dynamic library loading not supported
#endif
}

void *dynsim(void *handle, const char *symbol) {
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    return dlsym(handle, symbol);
#elif defined(_WIN32)
    return GetProcAddress(handle, symbol);
#else
    // Dynamic library loading not supported
    return NULL;
#endif
}