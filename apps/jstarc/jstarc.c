#include <argparse.h>
#include <assert.h>
#include <cwalk.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"

#define JSR_EXT ".jsr"
#define JSC_EXT ".jsc"

typedef struct Options {
    char *input, *output;
    bool recursive;
} Options;

static Options opts;
static JStarVM* vm;

static void initVM() {
    JStarConf conf = jsrGetConf();
    vm = jsrNewVM(&conf);
}

static void exitFree(int code) {
    jsrFreeVM(vm);
    exit(code);
}

static bool isDirectory(const char* path) {
    DIR* d = opendir(path);
    if(d != NULL) {
        if(closedir(d)) exitFree(errno);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// FILE COMPILE
// -----------------------------------------------------------------------------

static bool writeToFile(const JStarBuffer* buf, const char* path) {
    FILE* f = fopen(path, "wb+");
    if(f == NULL) {
        return false;
    }

    if(fwrite(buf->data, 1, buf->size, f) < buf->size) {
        return false;
    }

    return true;
}

static void compileFile(const char* path, const char* out) {
    JStarBuffer src;
    if(!jsrReadFile(vm, path, &src)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path, strerror(errno));
        exitFree(EXIT_FAILURE);
    }

    char normalizedOut[FILENAME_MAX];
    if(out != NULL) {
        cwk_path_normalize(out, normalizedOut, sizeof(normalizedOut));
    } else {
        cwk_path_change_extension(path, JSC_EXT, normalizedOut, sizeof(normalizedOut));
    }

    printf("Compiling %s to %s...\n", path, normalizedOut);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path, src.data, &compiled);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", path);
        jsrBufferFree(&src);
        exitFree(EXIT_FAILURE);
    }

    jsrBufferFree(&src);

    if(!writeToFile(&compiled, normalizedOut)) {
        fprintf(stderr, "Failed to write %s: %s\n", normalizedOut, strerror(errno));
        jsrBufferFree(&compiled);
        exitFree(EXIT_FAILURE);
    }
}

// -----------------------------------------------------------------------------
// DIRECTORY COMPILE
// -----------------------------------------------------------------------------

static void makeOutPath(const char* root, const char* curr, const char* name, const char* out,
                        char* res, size_t size) {
    if(out != NULL) {
        size_t rootLen = strlen(root);
        const char* fileRoot = curr + rootLen;

        if(strlen(fileRoot) != 0) {
            cwk_path_join(out, fileRoot, res, size);
            cwk_path_join(res, name, res, size);
        } else {
            cwk_path_join(out, name, res, size);
        }
    } else {
        cwk_path_join(curr, name, res, size);
    }

    cwk_path_change_extension(res, JSC_EXT, res, size);
}

static void compileDirectoryFile(const char* root, const char* curr, const char* name,
                                 const char* out) {
    char filePath[FILENAME_MAX];
    cwk_path_join(curr, name, filePath, sizeof(filePath));

    char outPath[FILENAME_MAX];
    makeOutPath(root, curr, name, out, outPath, sizeof(outPath));

    compileFile(filePath, outPath);
}

static void walkDirectory(const char* root, const char* curr, const char* out) {
    DIR* currentDir = opendir(curr);
    if(currentDir == NULL) {
        fprintf(stderr, "Cannot open directory %s: %s\n", curr, strerror(errno));
        exitFree(EXIT_FAILURE);
    }

    struct dirent* dp;
    while((dp = readdir(currentDir)) != NULL) {
        if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        switch(dp->d_type) {
        case DT_DIR: {
            if(opts.recursive) {
                char subDir[FILENAME_MAX];
                cwk_path_join(curr, dp->d_name, subDir, sizeof(subDir));
                walkDirectory(root, subDir, out);
            }
            break;
        }
        case DT_REG: {
            size_t len;
            const char* ext;
            cwk_path_get_extension(dp->d_name, &ext, &len);

            if(strcmp(ext, JSR_EXT) != 0) {
                continue;
            }

            compileDirectoryFile(root, curr, dp->d_name, out);
            break;
        }
        default:
            break;
        }
    }

    if(closedir(currentDir)) {
        fprintf(stderr, "Cannot close dir %s: %s\n", curr, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void compileDirectory(const char* dir, const char* out) {
    char inDir[FILENAME_MAX];
    cwk_path_normalize(dir, inDir, sizeof(inDir));
    walkDirectory(inDir, inDir, out);
}

// -----------------------------------------------------------------------------
// MAIN AND ARGUMENT PARSE
// -----------------------------------------------------------------------------

Options parseArguments(int argc, char** argv) {
    opts = (Options){0};

    static const char* const usage[] = {
        "jstarc [options] file",
        "jstarc [options] directory",
        NULL,
    };

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Options"),
        OPT_STRING('o', "output", &opts.output, "Output file or directory", 0, 0, 0),
        OPT_BOOLEAN(
            'r', "recursive", &opts.recursive,
            "Recursively compile files in <directory>, does nothing if passed argument is a file",
            0, 0, 0),
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

int main(int argc, char** argv) {
    parseArguments(argc, argv);
    initVM();

    if(isDirectory(opts.input)) {
        compileDirectory(opts.input, opts.output);
    } else {
        compileFile(opts.input, opts.output);
    }

    exitFree(EXIT_SUCCESS);
}
