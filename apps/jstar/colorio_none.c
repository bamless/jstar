#include "colorio.h"

int vfcolorPrintf(FILE* file, Color color, const char* fmt, va_list args) {
    return vfprintf(file, fmt, args);
}

int colorPrintf(Color color, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = vfcolorPrintf(stdout, color, fmt, ap);
    va_end(ap);
    return written;
}