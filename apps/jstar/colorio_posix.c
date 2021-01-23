#include <unistd.h>

#include "colorio.h"

static const char* colors[] = {
    [COLOR_BLACK] = "\x1b[30m",
    [COLOR_RED] = "\x1b[31m",
    [COLOR_GREEN] = "\x1b[32m",
    [COLOR_YELLOW] = "\x1b[33m",
    [COLOR_BLUE] = "\x1b[34m",
    [COLOR_MAGENTA] = "\x1b[35m",
    [COLOR_CYAN] = "\x1b[36m",
    [COLOR_WHITE] = "\x1b[37m",
    [COLOR_END] = "\x1b[0m",
};

static void startColor(FILE* f, Color color) {
    if(isatty(fileno(f))) {
        fprintf(f, colors[color]);
        fflush(f);
    }
}

static void endColor(FILE* f) {
    if(isatty(fileno(f))) {
        fprintf(f, colors[COLOR_END]);
        fflush(f);
    }
}

int vfcolorPrintf(FILE* file, Color color, const char* fmt, va_list args) {
    startColor(file, color);
    int written = vfprintf(file, fmt, args);
    endColor(file);
    return written;
}

int fcolorPrintf(FILE* file, Color color, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = vfcolorPrintf(file, color, fmt, ap);
    va_end(ap);
    return written;
}

int colorPrintf(Color color, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = vfcolorPrintf(stdout, color, fmt, ap);
    va_end(ap);
    return written;
}