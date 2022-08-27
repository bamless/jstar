#ifndef PATH_H
#define PATH_H

#include <stdbool.h>
#include <stdlib.h>

#if defined(_WIN32) && (defined(__WIN32__) || defined(WIN32) || defined(__MINGW32__))
    #define PATH_SEP      "\\"
    #define PATH_SEP_CHAR '\\'
#else
    #define PATH_SEP      "/"
    #define PATH_SEP_CHAR '/'
#endif

typedef struct Path {
    char* data;
    size_t size, capacity;
} Path;

Path pathNew(void);
Path pathCopy(const Path* o);
void pathInit(Path* p);
void pathFree(Path* p);

void pathClear(Path* p);
void pathAppend(Path* p, const char* str, size_t length);
void pathAppendStr(Path* p, const char* str);
void pathJoinStr(Path* p1, const char* str);
void pathJoin(Path* p, const Path* o);
void pathDirname(Path* p);
const char* pathGetExtension(Path* p, size_t* length);
bool pathHasExtension(Path* p);
void pathChangeExtension(Path* p, const char* newExt);
void pathNormalize(Path* p);
void pathToAbsolute(Path* p);
void pathReplace(Path* p, size_t off, char c, char r);
void pathTruncate(Path* p, size_t off);

Path pathAbsolute(const Path* p);
Path pathIntersect(const Path* p1, const Path* p2);

#endif