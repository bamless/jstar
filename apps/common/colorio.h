#ifndef COLORIO_H
#define COLORIO_H

#include <stdarg.h>
#include <stdio.h>

typedef enum Color {
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE,
    COLOR_END,
} Color;

int vfcolorPrintf(FILE* file, Color color, const char* fmt, va_list args);
int fcolorPrintf(FILE* file, Color color, const char* fmt, ...);
int colorPrintf(Color color, const char* fmt, ...);

#endif  // COLORIO_H
