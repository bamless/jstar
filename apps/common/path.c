#include "path.h"

#include <cwalk.h>
#include <stdlib.h>
#include <string.h>

#include "extlib.h"

static bool pathIsSeparator(char c) {
#if defined(_WIN32) && (defined(__WIN32__) || defined(WIN32) || defined(__MINGW32__))
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

Path pathNew_(const char* args[]) {
    Path p = {0};
    for(const char** segment = args; *segment; segment++) {
        pathJoinStr(&p, *segment);
    }
    if(!p.items) {
        sb_reserve(&p, 1);
        p.items[0] = '\0';
    }
    return p;
}

void pathFree(Path* p) {
    sb_free(p);
}

void pathClear(Path* p) {
    p->size = 0;
    if(p->items) p->items[0] = '\0';
}

void pathAppend(Path* p, const char* str, size_t length) {
    // The terminator lives just beyond the StringBuffer's logical contents,
    // keeping Path compatible with both counted strings and C APIs.
    sb_append(p, str, length);
    sb_reserve(p, p->size + 1);
    p->items[p->size] = '\0';
}

void pathAppendStr(Path* p, const char* str) {
    pathAppend(p, str, strlen(str));
}

void pathJoinStr(Path* p, const char* str) {
    if(!str || !*str) return;

    if(p->size) {
        char last = p->items[p->size - 1];
        if(!pathIsSeparator(last) && !pathIsSeparator(str[0])) {
            pathAppendStr(p, PATH_SEP);
        }
    }

    pathAppendStr(p, str);
}

void pathJoin(Path* p, const Path* other) {
    if(!other->size || !other->items) return;
    pathJoinStr(p, other->items);
}

void pathDirname(Path* p) {
    if(!p->size || !p->items) return;

    size_t length;
    cwk_path_get_dirname(p->items, &length);

    p->items[length] = '\0';
    p->size = length;
}

const char* pathGetExtension(const Path* p, size_t* length) {
    if(!p->size || !p->items) return NULL;
    const char* extension;
    if(!cwk_path_get_extension(p->items, &extension, length)) {
        return NULL;
    }
    return extension;
}

bool pathHasExtension(const Path* p) {
    return p->size && p->items && cwk_path_has_extension(p->items);
}

bool pathIsRelative(const Path* p) {
    return p->size && p->items && cwk_path_is_relative(p->items);
}

bool pathIsAbsolute(const Path* p) {
    return p->size && p->items && cwk_path_is_absolute(p->items);
}

void pathChangeExtension(Path* p, const char* newExt) {
    if(!p->size || !p->items) return;

    for(;;) {
        size_t newSize = cwk_path_change_extension(p->items, newExt, p->items, p->capacity);
        if(newSize >= p->capacity) {
            sb_reserve(p, newSize + 1);
            continue;
        } else {
            p->size = newSize;
            return;
        }
    }
}

void pathNormalize(Path* p) {
    if(!p->size || !p->items) return;

    for(;;) {
        size_t newSize = cwk_path_normalize(p->items, p->items, p->capacity);
        if(newSize >= p->capacity) {
            sb_reserve(p, newSize + 1);
            continue;
        } else {
            p->size = newSize;
            return;
        }
    }
}

bool pathToAbsolute(Path* p) {
    Path abs = pathAbsolute(p);
    if(!abs.items) return false;
    pathFree(p);
    *p = abs;
    return true;
}

void pathReplace(Path* p, size_t off, const char* chars, char replacement) {
    if(!p->size || !p->items) return;
    sb_replace(p, off, chars, replacement);
}

void pathTruncate(Path* p, size_t off) {
    if(!p->size || !p->items) {
        ASSERT(off == 0, "`off` out of bounds");
        return;
    }
    ASSERT(off <= p->size, "`off` out of bounds");
    p->items[off] = '\0';
    p->size = off;
}

size_t pathIntersectOffset(const Path* p, const Path* other) {
    if(!p->items || !other->items) return 0;
    return cwk_path_get_intersection(p->items, other->items);
}

Path pathIntersect(const Path* p1, const Path* p2) {
    Path result = {0};
    if(!p1->items || !p2->items) return result;
    size_t length = cwk_path_get_intersection(p1->items, p2->items);
    pathAppend(&result, p1->items, length);
    return result;
}

Path pathAbsolute(const Path* p) {
    Path abs = {0};
    if(!p->items) return abs;

    void* chk;
    defer_loop(chk = temp_checkpoint(), temp_rewind(chk)) {
        char* cwd = get_cwd_temp();
        if(cwd) {
            for(;;) {
                size_t newSize = cwk_path_get_absolute(cwd, p->items, abs.items, abs.capacity);
                if(newSize >= abs.capacity) {
                    sb_reserve(&abs, newSize + 1);
                    continue;
                } else {
                    abs.size = newSize;
                    break;
                }
            }
        }
    }

    return abs;
}
