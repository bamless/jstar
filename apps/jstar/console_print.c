#include "console_print.h"

#include <stdbool.h>
#include <stdio.h>

#include "jstar/conf.h"

#ifdef JSTAR_WINDOWS
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

#define COLOR_RESET "\033[0m"

static FILE* replxxStdToFile(ReplxxStdFile std) {
    switch(std) {
    case REPLXX_STDOUT:
        return stdout;
    case REPLXX_STDERR:
        return stderr;
    case REPLXX_STDIN:
        return stdin;
    }
    JSR_UNREACHABLE();
}

int vfConsolePrint(Replxx* replxx, ReplxxStdFile std, const char* color, const char* fmt,
                   va_list ap) {
    FILE* stdFile = replxxStdToFile(std);
    if(replxx_is_color_enabled(replxx) && isatty(fileno(stdFile))) {
        int written = 0;
        written += replxx_fprint(replxx, std, "%s", color);
        written += replxx_vfprint(replxx, std, fmt, ap);
        written += replxx_fprint(replxx, std, COLOR_RESET);
        return written;
    } else {
        return vfprintf(stdFile, fmt, ap);
    }
}

int fConsolePrint(Replxx* replxx, ReplxxStdFile std, const char* color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vfConsolePrint(replxx, std, color, fmt, args);
    va_end(args);
    return written;
}

int consolePrint(Replxx* replxx, const char* color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vfConsolePrint(replxx, REPLXX_STDOUT, color, fmt, args);
    va_end(args);
    return written;
}
