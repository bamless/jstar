#include <argparse.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"

typedef struct Options {
    char* input;
    char* out;
} Options;

Options parseArguments(int argc, char** argv) {
    Options opts = {0};

    static const char* const usage[] = {
        "jstarc [options] file out",
        NULL,
    };

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "jstarc: compile J* source files to bytecode", NULL);

    int nonOpts = argparse_parse(&argparse, argc, (const char**)argv);

    if(nonOpts != 2) {
        argparse_usage(&argparse);
        exit(-1);
    }

    opts.input = argv[0];
    opts.out = argv[1];
    return opts;
}

static bool writeToFile(const JStarBuffer* buf, const char* path) {
    FILE* f = fopen(path, "wb+");
    if(fwrite(buf->data, 1, buf->size, f) < buf->size) {
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    Options opts = parseArguments(argc, argv);

    JStarConf conf = jsrGetConf();
    JStarVM* vm = jsrNewVM(&conf);

    JStarBuffer src;
    if(!jsrReadFile(vm, opts.input, &src)) {
        fprintf(stderr, "Cannot open input file %s: %s\n", opts.input, strerror(errno));
        jsrFreeVM(vm);
        exit(EXIT_FAILURE);
    }

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, opts.input, src.data, &compiled);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", opts.input);
        jsrBufferFree(&src);
        jsrFreeVM(vm);
        exit(EXIT_FAILURE);
    }

    jsrBufferFree(&src);

    if(!writeToFile(&compiled, opts.out)) {
        fprintf(stderr, "Failed to write compiled file of %s: %s\n", opts.input, strerror(errno));
        jsrBufferFree(&compiled);
        jsrFreeVM(vm);
        exit(EXIT_FAILURE);
    }

    jsrFreeVM(vm);
}
