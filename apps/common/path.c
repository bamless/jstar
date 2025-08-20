#include "path.h"

#include <assert.h>
#include <cwalk.h>
#include <errno.h>
#include <string.h>

#include "extlib.h"
#include "profiler.h"

#define INIT_CAPACITY 16

Path pathNew(void) {
    return (Path){0};
}

Path pathCopy(const Path* o) {
    Path p = *o;
    p.data = malloc(p.capacity);
    memcpy(p.data, o->data, p.size);
    p.data[p.size] = '\0';
    return p;
}

void pathFree(Path* p) {
    free(p->data);
}

static void ensureCapacity(Path* p, size_t cap) {
    if(!p->capacity) {
        p->capacity = INIT_CAPACITY;
    }
    while(p->capacity < cap) {
        p->capacity *= 2;
    }
    p->data = realloc(p->data, p->capacity);
}

void pathClear(Path* p) {
    p->size = 0;
    if(p->data) p->data[0] = '\0';
}

void pathAppend(Path* p, const char* str, size_t length) {
    ensureCapacity(p, p->size + length + 1);
    memcpy(p->data + p->size, str, length);
    p->size += length;
    p->data[p->size] = '\0';
}

void pathAppendStr(Path* p, const char* str) {
    pathAppend(p, str, strlen(str));
}

void pathJoinStr(Path* p, const char* str) {
    PROFILE_FUNC()
    if(p->size && p->data[p->size - 1] != PATH_SEP_CHAR && *str != PATH_SEP_CHAR) {
        pathAppend(p, PATH_SEP, 1);
    }
    pathAppendStr(p, str);
}

void pathJoin(Path* p, const Path* o) {
    PROFILE_FUNC()
    pathJoinStr(p, o->data);
}

void pathDirname(Path* p) {
    size_t dirPos;
    cwk_path_get_dirname(p->data, &dirPos);
    p->size = dirPos;
    p->data[p->size] = '\0';
}

const char* pathGetExtension(const Path* p, size_t* length) {
    const char* ext;
    if(!cwk_path_get_extension(p->data, &ext, length)) {
        return NULL;
    }
    return ext;
}

bool pathHasExtension(const Path* p) {
    return p && cwk_path_has_extension(p->data);
}

bool pathIsRelative(const Path* p) {
    return p && cwk_path_is_relative(p->data);
}

bool pathIsAbsolute(const Path* p) {
    return p && cwk_path_is_absolute(p->data);
}

void pathChangeExtension(Path* p, const char* newExt) {
    size_t newSize = 0;
    do {
        if(newSize) ensureCapacity(p, newSize + 1);
        newSize = cwk_path_change_extension(p->data, newExt, p->data, p->capacity);
    } while(newSize >= p->capacity);
    p->size = newSize;
}

void pathNormalize(Path* p) {
    size_t newSize = 0;
    do {
        if(newSize) ensureCapacity(p, newSize + 1);
        newSize = cwk_path_normalize(p->data, p->data, p->capacity);
    } while(newSize >= p->capacity);
    p->size = newSize;
}

void pathToAbsolute(Path* p) {
    Path absolute = pathAbsolute(p);
    free(p->data);
    *p = absolute;
}

void pathReplace(Path* p, size_t off, char c, char r) {
    assert(off <= p->size);
    for(size_t i = off; i < p->size; i++) {
        if(p->data[i] == c) {
            p->data[i] = r;
        }
    }
}

void pathTruncate(Path* p, size_t off) {
    assert(off <= p->size);
    p->size = off;
    p->data[p->size] = '\0';
}

size_t pathIntersectOffset(const Path* p, const Path* o) {
    return cwk_path_get_intersection(p->data, o->data);
}

Path pathIntersect(const Path* p1, const Path* p2) {
    Path ret = pathNew();
    size_t intersect = cwk_path_get_intersection(p1->data, p2->data);
    pathAppend(&ret, p1->data, intersect);
    return ret;
}

Path pathAbsolute(const Path* p) {
    char* cwd = get_cwd();
    if(!cwd) return (Path){0};

    Path absolute = pathNew();
    size_t newSize = 0;
    do {
        if(newSize) ensureCapacity(&absolute, newSize + 1);
        newSize = cwk_path_get_absolute(cwd, p->data, absolute.data, absolute.capacity);
    } while(newSize >= absolute.capacity);

    absolute.size = newSize;
    free(cwd);
    return absolute;
}
