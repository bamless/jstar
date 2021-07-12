#include "console_print.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef _WIN32
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

static const char* colors[] = {
    [COLOR_RESET] = "\033[0m",
    [COLOR_BLACK] = "\033[0;22;30m",
    [COLOR_RED] = "\033[0;22;31m",
    [COLOR_GREEN] = "\033[0;22;32m",
    [COLOR_BROWN] = "\033[0;22;33m",
    [COLOR_BLUE] = "\033[0;22;34m",
    [COLOR_MAGENTA] = "\033[0;22;35m",
    [COLOR_CYAN] = "\033[0;22;36m",
    [COLOR_LIGHT_GRAY] = "\033[0;22;37m",
    [COLOR_GRAY] = "\033[0;1;90m",
    [COLOR_BRIGHTRED] = "\033[0;1;91m",
    [COLOR_BRIGHTGREEN] = "\033[0;1;92m",
    [COLOR_YELLOW] = "\033[0;1;93m",
    [COLOR_BRIGHTBLUE] = "\033[0;1;94m",
    [COLOR_BRIGHTMAGENTA] = "\033[0;1;95m",
    [COLOR_BRIGHTCYAN] = "\033[0;1;96m",
    [COLOR_WHITE] = "\033[0;1;97m",
    [COLOR_NONE] = "",
};

static FILE* replxxStdToFile(ReplxxStdFile std) {
    switch(std) {
    case REPLXX_STDOUT:
        return stdout;
    case REPLXX_STDERR:
        return stderr;
    case REPLXX_STDIN:
        return stdin;
    }
    assert(false);
    return NULL;
}

int vfConsolePrint(Replxx* replxx, ReplxxStdFile std, Color color, const char* fmt, va_list ap) {
    FILE* stdFile = replxxStdToFile(std);
    if(replxx_is_color_enabled(replxx) && isatty(fileno(stdFile))) {
        int written = 0;
        written += replxx_fprint(replxx, std, colors[color]);
        written += replxx_vfprint(replxx, std, fmt, ap);
        written += replxx_fprint(replxx, std, colors[COLOR_RESET]);
        return written;
    } else {
        return vfprintf(stdFile, fmt, ap);
    }
}

int fConsolePrint(Replxx* replxx, ReplxxStdFile std, Color color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vfConsolePrint(replxx, std, color, fmt, args);
    va_end(args);
    return written;
}

int consolePrint(Replxx* replxx, Color color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vfConsolePrint(replxx, REPLXX_STDOUT, color, fmt, args);
    va_end(args);
    return written;
}