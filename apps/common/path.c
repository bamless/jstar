#include "path.h"

#include <cwalk.h>
#include <stdlib.h>
#include <string.h>

#include "extlib.h"

Path pathNew_(const char* args[]) {
    Path p = {0};
    for(const char** segment = args; *segment; segment++) {
        pathJoinStr(&p, *segment);
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
    if(p->size) p->size--;
    sb_append(p, str, length);
    sb_append_char(p, '\0');
}

void pathAppendStr(Path* p, const char* cstr) {
    pathAppend(p, cstr, strlen(cstr));
}

void pathJoinStr(Path* p, const char* cstr) {
    if(p->size && p->items[p->size - 2] != PATH_SEP_CHAR && *cstr != PATH_SEP_CHAR) {
        pathAppendStr(p, PATH_SEP);
    }
    pathAppendStr(p, cstr);
}

void pathJoin(Path* p, const Path* o) {
    pathJoinStr(p, o->items);
}

void pathDirname(Path* p) {
    if(!p->size) return;
    size_t dirPos;
    cwk_path_get_dirname(p->items, &dirPos);
    p->size = dirPos;
    p->items[p->size] = '\0';
}

const char* pathGetExtension(const Path* p, size_t* length) {
    if(!p->size) return NULL;
    const char* ext;
    if(!cwk_path_get_extension(p->items, &ext, length)) return NULL;
    return ext;
}

bool pathHasExtension(const Path* p) {
    return p->size && cwk_path_has_extension(p->items);
}

bool pathIsRelative(const Path* p) {
    return p->size && cwk_path_is_relative(p->items);
}

bool pathIsAbsolute(const Path* p) {
    return p->size && cwk_path_is_absolute(p->items);
}

void pathChangeExtension(Path* p, const char* newExt) {
    if(!p->size) return;
    size_t newSize;
    for(;;) {
        newSize = cwk_path_change_extension(p->items, newExt, p->items, p->capacity);
        if(newSize >= p->capacity) {
            sb_reserve(p, newSize + 1);
        } else {
            break;
        }
    };
    p->size = newSize + 1;
}

void pathNormalize(Path* p) {
    if(!p->size) return;
    size_t newSize;
    for(;;) {
        newSize = cwk_path_normalize(p->items, p->items, p->capacity);
        if(newSize >= p->capacity) {
            sb_reserve(p, newSize + 1);
        } else {
            break;
        }
    }
    p->size = newSize + 1;
}

bool pathToAbsolute(Path* p) {
    Path absolute = pathAbsolute(p);
    pathFree(p);
    if(!absolute.items) return false;
    *p = absolute;
    return true;
}

void pathReplace(Path* p, size_t off, const char* chars, char r) {
    sb_replace(p, off, chars, r);
}

void pathTruncate(Path* p, size_t off) {
    ASSERT(off <= p->size, "`off` out of bounds");
    p->size = off;
    p->items[p->size] = '\0';
}

size_t pathIntersectOffset(const Path* p, const Path* o) {
    return cwk_path_get_intersection(p->items, o->items);
}

Path pathIntersect(const Path* p1, const Path* p2) {
    Path ret = {0};
    size_t intersect = cwk_path_get_intersection(p1->items, p2->items);
    pathAppend(&ret, p1->items, intersect);
    return ret;
}

Path pathAbsolute(const Path* p) {
    char* cwd = get_cwd();
    if(!cwd) return (Path){0};

    Path absolute = {0};
    size_t newSize;
    for(;;) {
        newSize = cwk_path_get_absolute(cwd, p->items, absolute.items, absolute.capacity);
        if(newSize >= absolute.capacity) {
            sb_reserve(&absolute, newSize + 1);
        } else {
            break;
        }
    }
    absolute.size = newSize + 1;
    free(cwd);

    return absolute;
}
