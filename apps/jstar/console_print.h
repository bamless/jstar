#ifndef CONSOLE_PRINT_H
#define CONSOLE_PRINT_H

#include <stdarg.h>

#include "replxx.h"

typedef enum Color {
    COLOR_RESET,
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BROWN,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_LIGHT_GRAY,
    COLOR_DEFAULT,
} Color;

int vfConsolePrint(Replxx* replxx, ReplxxStdFile std, Color color, const char* fmt, va_list ap);
int fConsolePrint(Replxx* replxx, ReplxxStdFile std, Color color, const char* fmt, ...);
int consolePrint(Replxx* replxx, Color color, const char* fmt, ...);

#endif