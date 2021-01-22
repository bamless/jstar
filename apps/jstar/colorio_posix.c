#include <unistd.h>

#include "colorio.h"

static const char* colors[] = {
    [COLOR_RED] = "\x1b[31m",
    [COLOR_BLUE] = "\x1b[34m",
    [COLOR_END] = "\x1b[0m",
};

static void startColor(FILE* f, Color color) {
    if(isatty(fileno(f))) fprintf(f, colors[color]);
}

static void endColor(FILE* f) {
    if(isatty(fileno(f))) fprintf(f, colors[COLOR_END]);
}

int vfcolorPrintf(FILE* file, Color color, const char* fmt, va_list args) {
    startColor(stdout, color);
    int written = vfprintf(file, fmt, args);
    endColor(stdout);
    return written;
}

int colorPrintf(Color color, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = vfcolorPrintf(stdout, color, fmt, ap);
    va_end(ap);
    return written;
}