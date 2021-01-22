#ifndef COLORIO_H
#define COLORIO_H

#include <stdarg.h>
#include <stdio.h>

typedef enum Color {
    COLOR_RED,
    COLOR_BLUE,
    COLOR_END,
} Color;

int vfcolorPrintf(FILE* file, Color color, const char* fmt, va_list args);
int colorPrintf(Color color, const char* fmt, ...);

#endif  // COLORIO_H
