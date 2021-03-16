#include "colorio.h"

int vfcolorPrintf(FILE* file, Color color, const char* fmt, va_list args) {
    return vfprintf(file, fmt, args);
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