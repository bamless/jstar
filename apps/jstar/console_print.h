#ifndef PRINT_H
#define PRINT_H

#include <replxx.h>
#include <stdarg.h>

#define COLOR_RED   "\033[0;22;31m"
#define COLOR_GREEN "\033[0;22;32m"
#define COLOR_BLUE  "\033[0;22;34m"
#define COLOR_CYAN  "\033[0;22;36m"
#define COLOR_WHITE "\033[0;1;97m"

// Wraps replxx colored output functions with a more familiar printf-like syntax
int vfConsolePrint(Replxx* replxx, ReplxxStdFile std, const char* color, const char* fmt,
                   va_list ap);
int fConsolePrint(Replxx* replxx, ReplxxStdFile std, const char* color, const char* fmt, ...);
int consolePrint(Replxx* replxx, const char* color, const char* fmt, ...);

#endif
