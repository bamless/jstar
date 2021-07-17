#include <argparse.h>
#include <cwalk.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"
#include "profiler.h"

#define JSR_EXT ".jsr"
#define JSC_EXT ".jsc"

typedef struct Options {
    char *input, *output;
    bool showVersion;
    bool disassemble;
    bool compileOnly;
    bool recursive;
    bool list;
} Options;

static Options opts;
static JStarVM* vm;

// -----------------------------------------------------------------------------
// CALLBACKS AND HOOKS
// -----------------------------------------------------------------------------

// Custom J* error callback
static void errorCallback(JStarVM* vm, JStarResult res, const char* file, int ln, const char* err) {
    PROFILE_FUNC()
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

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

// Print the J* version along with its compilation environment
static void printVersion(void) {
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);
}

// Returns whether `path` is a directory or not 
static bool isDirectory(const char* path) {
    DIR* d = opendir(path);
    if(d != NULL) {
        if(closedir(d)) exit(errno);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// FILE COMPILATION AND DISASSEMBLY
// -----------------------------------------------------------------------------

// Write a JStarBuffer to file.
// The buffer is written as a binary file in order to avoid \n -> \r\n conversions on windows.
static bool writeToFile(const JStarBuffer* buf, const char* path) {
    PROFILE_FUNC()

    FILE* f = fopen(path, "wb");
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

// Compile the file at `path` and store the result in a new file at `out`.
// If `out` is NULL, then an output path will be generated from the input one by changing
// the file extension.
// If `-l` or `-c` were passed to the application, then no output file is generated.
// Returns true on success, false on failure.
static bool compileFile(const char* path, const char* out) {
    PROFILE_FUNC()

    JStarBuffer src;
    if(!jsrReadFile(vm, path, &src)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path, strerror(errno));
        return false;
    }

    char outPath[FILENAME_MAX];
    if(out != NULL) {
        cwk_path_normalize(out, outPath, sizeof(outPath));
    } else {
        cwk_path_change_extension(path, JSC_EXT, outPath, sizeof(outPath));
    }

    printf("Compiling %s to %s...\n", path, outPath);
    fflush(stdout);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path, src.data, &compiled);
    jsrBufferFree(&src);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", path);
        return false;
    }

    if(opts.list) {
        jsrDisassembleCode(vm, path, &compiled);
    } else if(!opts.compileOnly) {
        if(!writeToFile(&compiled, outPath)) {
            fprintf(stderr, "Failed to write %s: %s\n", outPath, strerror(errno));
            jsrBufferFree(&compiled);
            return false;
        }
    }

    jsrBufferFree(&compiled);
    return true;
}

// Disassemble the file at `path` and print the bytecode to standard output.
// Returns true on success, false on failure.
static bool disassembleFile(const char* path) {
    PROFILE_FUNC()

    JStarBuffer code;
    if(!jsrReadFile(vm, path, &code)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path, strerror(errno));
        return false;
    }

    printf("Disassembling %s...\n", path);
    fflush(stdout);

    JStarResult res = jsrDisassembleCode(vm, path, &code);
    jsrBufferFree(&code);
    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error disassembling file %s\n", path);
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// DIRECTORY COMPILATION
// -----------------------------------------------------------------------------

// Generate an output path using the starting directory, current position in the
// directory tree and a filename.
static void makeOutputPath(const char* root, const char* curr, const char* file, const char* out,
                           char* dest, size_t size) {
    const char* fileRoot = curr + cwk_path_get_intersection(root, curr);

    if(strlen(fileRoot) != 0) {
        const char* components[] = {out, fileRoot, file, NULL};
        cwk_path_join_multiple(components, dest, size);
    } else {
        cwk_path_join(out, file, dest, size);
    }

    cwk_path_change_extension(dest, JSC_EXT, dest, size);
}

// Process a J* source file during directory compilation.
// It generates the the full file path and an output path using on the root directory,
// current position in the directory tree and a file name.
// It then compiles or disassembles the file based on application options.
// Returns true on success, false on failure.
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

static bool isRelPath(const char* path) {
    return strcmp(path, ".") == 0 || strcmp(path, "..") == 0;
}

// Walk a directory (recursively, if `-r` was specified) and process all files
// that end in a`.jsr` extension.
// Returns true on success, false on failure.
static bool walkDirectory(const char* root, const char* curr, const char* out) {
    DIR* currentDir = opendir(curr);
    if(currentDir == NULL) {
        fprintf(stderr, "Cannot open directory %s: %s\n", curr, strerror(errno));
        return false;
    }

    bool allok = true;
    struct dirent* file;
    while((file = readdir(currentDir)) != NULL) {
        if(isRelPath(file->d_name)) continue;

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
            if(cwk_path_get_extension(file->d_name, &ext, &len) &&
               strcmp(ext, opts.disassemble ? JSC_EXT : JSR_EXT) == 0) {
                allok &= processDirFile(root, curr, file->d_name, out);
            }
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

// Process a directory.
// It normalizes the diretory path and either normalizes or generates an out
// path depending if the `out` argument is null or not.
// It then delegates the actual directory scan to `walkDirectory`.
// Returns true on success, false on failure.
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
// ARGUMENT PARSE
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
        OPT_BOOLEAN('v', "version", &opts.showVersion, "Print version information and exit", 0, 0,
                    0),
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
                    "Disassemble already compiled jsc files and list their content", 0, 0, 0),
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

// -----------------------------------------------------------------------------
// APP INITIALIZATION AND MAIN FUNCTION
// -----------------------------------------------------------------------------

static void initApp(int argc, char** argv) {
    parseArguments(argc, argv);

    // Bail out early if we only need to show the version
    if(opts.showVersion) {
        printVersion();
        exit(EXIT_SUCCESS);
    }

    PROFILE_BEGIN_SESSION("jstar-init.json")

    JStarConf conf = jsrGetConf();
    conf.errorCallback = &errorCallback;
    vm = jsrNewVM(&conf);

    PROFILE_END_SESSION()
}

static void freeApp(void) {
    PROFILE_BEGIN_SESSION("jstar-free.json")
    jsrFreeVM(vm);
    PROFILE_END_SESSION()
}

int main(int argc, char** argv) {
    parseArguments(argc, argv);

    initApp(argc, argv);
    atexit(&freeApp);

    PROFILE_BEGIN_SESSION("jstar-run.json")

    bool ok;
    if(isDirectory(opts.input)) {
        ok = processDirectory(opts.input, opts.output);
    } else if(opts.disassemble) {
        ok = disassembleFile(opts.input);
    } else {
        ok = compileFile(opts.input, opts.output);
    }

    PROFILE_END_SESSION()

    exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
