#include <argparse.h>
#include <cwalk.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
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

static void errorCallback(JStarVM* vm, JStarResult err, const char* file, int line,
                          const char* error) {
    switch(err) {
    case JSR_SYNTAX_ERR:
    case JSR_COMPILE_ERR:
        fprintf(stderr, "File %s [line:%d]:\n", file, line);
        fprintf(stderr, "%s\n", error);
        break;
    default:
        break;
    }
}

static void initVM(void) {
    JStarConf conf = jsrGetConf();
    conf.errorCallback = &errorCallback;
    vm = jsrNewVM(&conf);
}

static void freeVM(void) {
    jsrFreeVM(vm);
}

static bool isDirectory(const char* path) {
    DIR* d = opendir(path);
    if(d != NULL) {
        if(closedir(d)) exit(errno);
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
        fclose(f);
        return false;
    }

    if(fclose(f)) {
        return false;
    }

    return true;
}

static void compileFile(const char* path, const char* out) {
    JStarBuffer src;
    if(!jsrReadFile(vm, path, &src)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char outPath[FILENAME_MAX];
    if(out != NULL) {
        strncpy(outPath, out, sizeof(outPath));
    } else {
        cwk_path_change_extension(path, JSC_EXT, outPath, sizeof(outPath));
    }

    printf("Compiling %s to %s...\n", path, outPath);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path, src.data, &compiled);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", path);
        jsrBufferFree(&src);
        exit(EXIT_FAILURE);
    }

    jsrBufferFree(&src);

    if(!writeToFile(&compiled, outPath)) {
        fprintf(stderr, "Failed to write %s: %s\n", outPath, strerror(errno));
        jsrBufferFree(&compiled);
        exit(EXIT_FAILURE);
    }

    jsrBufferFree(&compiled);
}

// -----------------------------------------------------------------------------
// DIRECTORY COMPILE
// -----------------------------------------------------------------------------

static void makeOutPath(const char* root, const char* curr, const char* name, const char* out,
                        char* dest, size_t size) {
    size_t rootLen = strlen(root);
    const char* fileRoot = curr + rootLen;

    if(strlen(fileRoot) != 0) {
        const char* components[] = {out, fileRoot, dest, NULL};
        cwk_path_join_multiple(components, dest, size);
    } else {
        cwk_path_join(out, name, dest, size);
    }

    cwk_path_change_extension(dest, JSC_EXT, dest, size);
}

static void compileFileInDirectory(const char* root, const char* curr, const char* name,
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
        exit(EXIT_FAILURE);
    }

    struct dirent* file;
    while((file = readdir(currentDir)) != NULL) {
        if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            continue;
        }

        switch(file->d_type) {
        case DT_DIR: {
            if(opts.recursive) {
                char subDirectory[FILENAME_MAX];
                cwk_path_join(curr, file->d_name, subDirectory, sizeof(subDirectory));
                walkDirectory(root, subDirectory, out);
            }
            break;
        }
        case DT_REG: {
            size_t len;
            const char* ext;

            cwk_path_get_extension(file->d_name, &ext, &len);
            if(strcmp(ext, JSR_EXT) != 0) {
                continue;
            }

            compileFileInDirectory(root, curr, file->d_name, out);
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
    char inputDir[FILENAME_MAX];
    cwk_path_normalize(dir, inputDir, sizeof(inputDir));

    char outputDir[FILENAME_MAX];
    if(out != NULL) {
        cwk_path_normalize(out, outputDir, sizeof(outputDir));
    } else {
        strcpy(outputDir, inputDir);
    }

    walkDirectory(inputDir, inputDir, outputDir);
}

// -----------------------------------------------------------------------------
// MAIN AND ARGUMENT PARSE
// -----------------------------------------------------------------------------

static void parseArguments(int argc, char** argv) {
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
}

int main(int argc, char** argv) {
    parseArguments(argc, argv);

    initVM();
    atexit(&freeVM);

    if(isDirectory(opts.input)) {
        compileDirectory(opts.input, opts.output);
    } else {
        compileFile(opts.input, opts.output);
    }

    exit(EXIT_SUCCESS);
}
