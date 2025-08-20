#ifndef PATH_H
#define PATH_H

#include <stdbool.h>
#include <stdlib.h>

#include "extlib.h"

#if defined(_WIN32) && (defined(__WIN32__) || defined(WIN32) || defined(__MINGW32__))
    #define PATH_SEP      "\\"
    #define PATH_SEP_CHAR '\\'
#else
    #define PATH_SEP      "/"
    #define PATH_SEP_CHAR '/'
#endif

typedef StringBuffer Path;

#define pathNew(...) pathNew_((const char*[]){__VA_ARGS__, NULL});
Path pathNew_(const char* args[]);
void pathFree(Path* p);

void pathClear(Path* p);
void pathAppend(Path* p, const char* str, size_t length);
void pathAppendStr(Path* p, const char* str);
void pathJoinStr(Path* p1, const char* str);
void pathJoin(Path* p, const Path* o);
void pathDirname(Path* p);
const char* pathGetExtension(const Path* p, size_t* length);
bool pathHasExtension(const Path* p);
bool pathIsRelative(const Path* p);
bool pathIsAbsolute(const Path* p);
void pathChangeExtension(Path* p, const char* newExt);
void pathNormalize(Path* p);
void pathToAbsolute(Path* p);
void pathReplace(Path* p, size_t off, const char* chars, char r);
void pathTruncate(Path* p, size_t off);
size_t pathIntersectOffset(const Path* p, const Path* o);

Path pathIntersect(const Path* p1, const Path* p2);
Path pathAbsolute(const Path* p);

#endif
