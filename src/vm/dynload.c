#include "dynload.h"

#include <dlfcn.h>

// TODO: implement windows dynamic library loading

void *dynload(const char *path) {
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    return dlopen(path, RTLD_NOW);
#elif defined(_WIN32)
    return NULL;
#endif
}

void dynfree(void *handle) {
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    dlclose(handle);
#elif defined(_WIN32)

#endif
}

void *dynsim(void *handle, const char *symbol) {
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    return dlsym(handle, symbol);
#elif defined(_WIN32)
    return NULL;
#endif
}