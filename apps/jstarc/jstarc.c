#include <argparse.h>
#include <assert.h>
#include <cwalk.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"

typedef struct Options {
    char *input, *output;
} Options;

static JStarVM* vm;

static void initVM() {
    JStarConf conf = jsrGetConf();
    vm = jsrNewVM(&conf);
}

static void exitFree(int code) {
    jsrFreeVM(vm);
    exit(code);
}

Options parseArguments(int argc, char** argv) {
    Options opts = {0};

    static const char* const usage[] = {
        "jstarc [options] file",
        NULL,
    };

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_STRING('o', "output", &opts.output, "Output file", 0, 0, 0),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "jstarc compiles J* source files to bytecode", NULL);
    int nonOpts = argparse_parse(&argparse, argc, (const char**)argv);

    if(nonOpts != 1) {
        argparse_usage(&argparse);
        exit(EXIT_FAILURE);
    }

    opts.input = argv[0];
    return opts;
}

static bool writeToFile(const JStarBuffer* buf, const char* path) {
    FILE* f = fopen(path, "wb+");
    if(fwrite(buf->data, 1, buf->size, f) < buf->size) {
        return false;
    }
    return true;
}

static bool isDirectory(const char* path) {
    DIR* d = opendir(path);
    if(d != NULL) {
        if(closedir(d)) exitFree(errno);
        return true;
    }
    return false;
}

static void compileFile(const char* path, const char* out) {
    JStarBuffer src;
    if(!jsrReadFile(vm, path, &src)) {
        fprintf(stderr, "Cannot open input file %s: %s\n", path, strerror(errno));
        exitFree(EXIT_FAILURE);
    }

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path, src.data, &compiled);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", path);
        jsrBufferFree(&src);
        exitFree(EXIT_FAILURE);
    }

    jsrBufferFree(&src);

    char outPath[FILENAME_MAX] = {0};
    if(out) {
        strncat(outPath, out, sizeof(outPath) - 1);
    } else {
        cwk_path_change_extension(path, "jsc", outPath, sizeof(outPath));
    }

    if(!writeToFile(&compiled, outPath)) {
        fprintf(stderr, "Failed to write compilation output of %s: %s\n", path, strerror(errno));
        jsrBufferFree(&compiled);
        exitFree(EXIT_FAILURE);
    }
}

static void compileDirectory(const char* path, const char* out) {
    printf("Compiling to direcoty WIP\n");
    return;

    DIR* dir = opendir(path);
    assert(dir);

    struct dirent* dirent = readdir(dir);
    if(dirent == NULL) {
        fprintf(stderr, "Cannot read directory %s: %s\n", path, strerror(errno));
        exitFree(EXIT_FAILURE);
    }
}

int main(int argc, char** argv) {
    Options opts = parseArguments(argc, argv);

    initVM();

    if(isDirectory(opts.input)) {
        compileDirectory(opts.input, opts.output);
    } else {
        compileFile(opts.input, opts.output);
    }

    exitFree(EXIT_SUCCESS);
}
