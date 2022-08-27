#ifndef HIGHLIGHTER_H
#define HIGHLIGHTER_H

#include <replxx.h>

// Replxx highlighter callback with J* syntax support
void highlighter(const char* input, ReplxxColor* colors, int size, void* userData);

#endif