#ifndef DYNLOAD_H
#define DYNLOAD_H

#include "jstar/jstar.h"

/**
 * Wrap platform specific shared library load functions.
 * Used by the import system to resolve native module extensions.
 */

#if defined(JSTAR_POSIX)
    #include <dlfcn.h>
    #define dynload(path)          dlopen(path, RTLD_LAZY)
    #define dynfree(handle)        dlclose(handle)
    #define dynsim(handle, symbol) dlsym(handle, symbol)
#elif defined(JSTAR_WINDOWS)
    #include <Windows.h>
    #define dynload(path)          LoadLibrary(path)
    #define dynfree(handle)        FreeLibrary(handle)
    #define dynsim(handle, symbol) (void*)GetProcAddress(handle, symbol)
#else
    // Fallback for platforms for which we don't support shared library native extensions
    #define dynload(path)          ((void*)0)
    #define dynfree(handle)        ((void)0)
    #define dynsim(handle, symbol) ((void*)0)
#endif

#endif
