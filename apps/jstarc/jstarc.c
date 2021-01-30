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
    bool disassemble;
    bool compileOnly;
    bool recursive;
    bool list;
} Options;

static Options opts;
static JStarVM* vm;

static void errorCallback(JStarVM* vm, JStarResult res, const char* file, int ln, const char* err) {
    switch(res) {
    case JSR_SYNTAX_ERR:
    case JSR_COMPILE_ERR:
        fprintf(stderr, "File %s [line:%d]:\n", file, ln);
        fprintf(stderr, "%s\n", err);
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
        int saveErrno = errno;
        if(fclose(f)) return false;
        errno = saveErrno;
        return false;
    }

    if(fclose(f)) {
        return false;
    }

    return true;
}

static bool compileFile(const char* path, const char* out) {
    JStarBuffer src;
    if(!jsrReadFile(vm, path, &src)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path, strerror(errno));
        return false;
    }

    char outPath[FILENAME_MAX];
    if(out != NULL) {
        strncpy(outPath, out, sizeof(outPath));
    } else {
        cwk_path_change_extension(path, JSC_EXT, outPath, sizeof(outPath));
    }

    printf("Compiling %s to %s...\n", path, outPath);
    fflush(stdout);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path, src.data, &compiled);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", path);
        jsrBufferFree(&src);
        return false;
    }

    jsrBufferFree(&src);

    if(opts.list) {
        jsrDisassembleCode(vm, &compiled);
    } else if(!opts.compileOnly && !writeToFile(&compiled, outPath)) {
        fprintf(stderr, "Failed to write %s: %s\n", outPath, strerror(errno));
        jsrBufferFree(&compiled);
        return false;
    }

    jsrBufferFree(&compiled);
    return true;
}

static bool disassembleFile(const char* path) {
    JStarBuffer code;
    if(!jsrReadFile(vm, path, &code)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path, strerror(errno));
        return false;
    }

    printf("Disassembling %s...\n", path);
    fflush(stdout);

    JStarResult res = jsrDisassembleCode(vm, &code);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error disassembling file %s\n", path);
        jsrBufferFree(&code);
        return false;
    }

    jsrBufferFree(&code);
    return true;
}

// -----------------------------------------------------------------------------
// DIRECTORY COMPILE
// -----------------------------------------------------------------------------

static void makeOutputPath(const char* root, const char* curr, const char* file, const char* out,
                           char* dest, size_t size) {
    const char* fileRoot = curr + strlen(root);

    if(strlen(fileRoot) != 0) {
        const char* components[] = {out, fileRoot, dest, NULL};
        cwk_path_join_multiple(components, dest, size);
    } else {
        cwk_path_join(out, file, dest, size);
    }

    cwk_path_change_extension(dest, JSC_EXT, dest, size);
}

static bool processDirFile(const char* root, const char* curr, const char* file, const char* out) {
    char filePath[FILENAME_MAX];
    cwk_path_join(curr, file, filePath, sizeof(filePath));

    if(opts.disassemble) {
        return disassembleFile(filePath);
    } else {
        char outPath[FILENAME_MAX];
        makeOutputPath(root, curr, file, out, outPath, sizeof(outPath));
        return compileFile(filePath, outPath);
    }
}

static bool walkDirectory(const char* root, const char* curr, const char* out) {
    DIR* currentDir = opendir(curr);
    if(currentDir == NULL) {
        fprintf(stderr, "Cannot open directory %s: %s\n", curr, strerror(errno));
        return false;
    }

    bool allok = true;
    struct dirent* file;
    while((file = readdir(currentDir)) != NULL) {
        if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) continue;

        switch(file->d_type) {
        case DT_DIR: {
            if(opts.recursive) {
                char subDirectory[FILENAME_MAX];
                cwk_path_join(curr, file->d_name, subDirectory, sizeof(subDirectory));
                allok &= walkDirectory(root, subDirectory, out);
            }
            break;
        }
        case DT_REG: {
            size_t len;
            const char* ext;
            cwk_path_get_extension(file->d_name, &ext, &len);
            if(strcmp(ext, opts.disassemble ? JSC_EXT : JSR_EXT) != 0) continue;
            allok &= processDirFile(root, curr, file->d_name, out);
            break;
        }
        default:
            break;
        }
    }

    if(closedir(currentDir)) {
        fprintf(stderr, "Cannot close dir %s: %s\n", curr, strerror(errno));
        return false;
    }

    return allok;
}

static bool processDirectory(const char* dir, const char* out) {
    char inputDir[FILENAME_MAX];
    cwk_path_normalize(dir, inputDir, sizeof(inputDir));

    char outputDir[FILENAME_MAX];
    if(out != NULL) {
        cwk_path_normalize(out, outputDir, sizeof(outputDir));
    } else {
        strcpy(outputDir, inputDir);
    }

    return walkDirectory(inputDir, inputDir, outputDir);
}

// -----------------------------------------------------------------------------
// MAIN AND ARGUMENT PARSE
// -----------------------------------------------------------------------------

static void parseArguments(int argc, char** argv) {
    opts = (Options){0};

    static const char* const usage[] = {
        "jstarc [options] <file>",
        "jstarc [options] <directory>",
        NULL,
    };

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Options"),
        OPT_STRING('o', "output", &opts.output, "Output file or directory", 0, 0, 0),
        OPT_BOOLEAN('r', "recursive", &opts.recursive,
                    "Recursively compile/disassemble files in <directory>, does nothing if passed "
                    "argument is a <file>",
                    0, 0, 0),
        OPT_BOOLEAN('l', "list", &opts.list,
                    "List the compiled bytecode instead of saving it on file. Listing compiled "
                    "bytecode is useful to learn about the J* VM",
                    0, 0, 0),
        OPT_BOOLEAN('d', "disassemble", &opts.disassemble,
                    "Disassemble already compiled jsc files and list its content", 0, 0, 0),
        OPT_BOOLEAN('c', "compile-only", &opts.compileOnly,
                    "Compile files but do not generate output files. Used for syntax checking", 0,
                    0, 0),
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

    bool ok;
    if(isDirectory(opts.input)) {
        ok = processDirectory(opts.input, opts.output);
    } else if(opts.disassemble) {
        ok = disassembleFile(opts.input);
    } else {
        ok = compileFile(opts.input, opts.output);
    }

    exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
