#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"

#define EXTENSION ".jsc"

static char* findExtension(const char* str, int c) {
    const char* ptr = str + strlen(str);
    while(ptr != str && *ptr != '/' && *ptr != '\\') {
        if(*ptr == c) {
            return (char*)ptr;
        }
        ptr--;
    }
    return NULL;
}

int main(int argc, char** argv) {
    if(argc <= 1) {
        fprintf(stderr, "No files specified\n");
        return -1;
    }

    JStarConf conf = jsrGetConf();
    JStarVM* vm = jsrNewVM(&conf);

    for(int i = 1; i < argc; i++) {
        const char* file = argv[i];

        char* src = jsrReadFile(file);
        if(src == NULL) {
            fprintf(stderr, "Cannot read file %s\n", strerror(errno));
            continue;
        }

        size_t size;
        void* blob = jsrCompile(vm, src, &size);
        free(src);

        char* ext = findExtension(file, '.');

        size_t pathLen = ext ? (size_t)(ext - file) : strlen(file) + 1;
        char* out = malloc(pathLen + strlen(EXTENSION) + 2);
        memcpy(out, file, pathLen);
        out[pathLen] = '\0';
        strcat(out, EXTENSION);

        FILE* outFile = fopen(out, "wb+");
        if(outFile == NULL) {
            fprintf(stderr, "Cannot create file %s\n", strerror(errno));
            free(out);
            free(blob);
            continue;
        }

        size_t res = fwrite(blob, 1, size, outFile);
        if(res < size) {
            fprintf(stderr, "error writing to file %s\n", strerror(errno));
        }

        fclose(outFile);
        free(out);
        free(blob);
    }

    jsrFreeVM(vm);
}