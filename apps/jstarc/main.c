#include <argparse.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"
#include "jstar/parse/lex.h"
#include "path.h"
#include "profiler.h"

#ifdef _WIN32
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

#define JSR_EXT ".jsr"
#define JSC_EXT ".jsc"

#define COLOR_RED   "\033[0;22;31m"
#define COLOR_RESET "\033[0m"

typedef struct Options {
    char *input, *output;
    bool disassemble;
    bool compileOnly;
    bool recursive;
    bool showVersion;
    bool disableColors;
    bool list;
} Options;

static Options opts;
static JStarVM* vm;

// Custom J* error callback.
static void errorCallback(JStarVM* vm, JStarResult res, const char* file, JStarLoc loc,
                          const char* err) {
    PROFILE_FUNC()
    switch(res) {
    case JSR_SYNTAX_ERR:
    case JSR_COMPILE_ERR:
        if(!opts.disableColors && isatty(fileno(stderr))){
            fprintf(stderr, COLOR_RED);
        }
        fprintf(stderr, "%s:%d:%d: error\n", file, loc.line, loc.col);
        fprintf(stderr, "%s\n", err);
        if(!opts.disableColors && isatty(fileno(stderr))){
            fprintf(stderr, COLOR_RESET);
        }
        break;
    default:
        break;
    }
}

// Print the J* version along with its compilation environment.
static void printVersion(void) {
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);
}

// Returns whether `path` is a directory or not.
static bool isDirectory(const char* path) {
    DIR* d = opendir(path);
    if(d != NULL) {
        if(closedir(d)) exit(errno);
        return true;
    }
    return false;
}

// Write a JStarBuffer to file.
// The buffer is written as a binary file in order to avoid \n -> \r\n conversions on windows.
static bool writeToFile(const JStarBuffer* buf, const Path* path) {
    PROFILE_FUNC()

    FILE* f = fopen(path->data, "wb");
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
static bool compileFile(const Path* path, const Path* out) {
    PROFILE_FUNC()

    JStarBuffer src;
    if(!jsrReadFile(vm, path->data, &src)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path->data, strerror(errno));
        return false;
    }

    printf("Compiling %s to %s...\n", path->data, out->data);
    fflush(stdout);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path->data, src.data, &compiled);
    jsrBufferFree(&src);

    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file %s\n", path->data);
        return false;
    }

    if(opts.list) {
        jsrDisassembleCode(vm, path->data, compiled.data, compiled.size);
    } else if(!opts.compileOnly) {
        if(!writeToFile(&compiled, out)) {
            fprintf(stderr, "Failed to write %s: %s\n", out->data, strerror(errno));
            jsrBufferFree(&compiled);
            return false;
        }
    }

    jsrBufferFree(&compiled);
    return true;
}

// Disassemble the file at `path` and print the bytecode to standard output.
// Returns true on success, false on failure.
static bool disassembleFile(const Path* path) {
    PROFILE_FUNC()

    JStarBuffer code;
    if(!jsrReadFile(vm, path->data, &code)) {
        fprintf(stderr, "Cannot open file %s: %s\n", path->data, strerror(errno));
        return false;
    }

    printf("Disassembling %s...\n", path->data);
    fflush(stdout);

    JStarResult res = jsrDisassembleCode(vm, path->data, code.data, code.size);
    jsrBufferFree(&code);

    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error disassembling file %s\n", path->data);
        return false;
    }

    return true;
}

// Generates the the full output path using the input root directory, output root directory,
// the current position in the directory tree and a file name.
static Path makeOutputPath(const Path* in, const Path* out, const Path* curr,
                           const char* fileName) {
    Path outPath = pathCopy(out);

    size_t commonPath = pathIntersectOffset(in, curr);
    if(commonPath) {
        pathJoinStr(&outPath, curr->data + commonPath);
    }

    pathJoinStr(&outPath, fileName);
    pathChangeExtension(&outPath, JSC_EXT);

    return outPath;
}

// Compiles (or disassembles) a J* source file during directory compilation.
// Compiles or disassembles the file based on application options.
// Returns true on success, false on failure.
static bool compileDirFile(const Path* in, const Path* out, const Path* curr,
                           const char* fileName) {
    Path filePath = pathCopy(curr);
    pathJoinStr(&filePath, fileName);

    bool ok;
    if(!opts.disassemble) {
        Path outPath = makeOutputPath(in, out, curr, fileName);
        ok = compileFile(&filePath, &outPath);
        pathFree(&outPath);
    } else {
        ok = disassembleFile(&filePath);
    }

    pathFree(&filePath);
    return ok;
}

// Walk a directory (recursively, if `-r` was specified) and process
// all files that end in a `.jsr` or `.jsc` extension.
// Returns true on success, false on failure.
static bool compileDirectory(const Path* in, const Path* out, const Path* curr) {
    DIR* directory = opendir(curr->data);
    if(directory == NULL) {
        fprintf(stderr, "Cannot open directory %s: %s\n", curr->data, strerror(errno));
        return false;
    }

    bool allok = true;
    struct dirent* dirent;
    while((dirent = readdir(directory)) != NULL) {
        if(strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0) {
            continue;
        }

        switch(dirent->d_type) {
        case DT_DIR: {
            if(opts.recursive) {
                Path subDirectory = pathCopy(curr);
                pathJoinStr(&subDirectory, dirent->d_name);
                allok &= compileDirectory(in, out, &subDirectory);
                pathFree(&subDirectory);
            }
            break;
        }
        case DT_REG: {
            size_t fileNameLen = strlen(dirent->d_name);

            const char* extension = NULL;
            if(fileNameLen > strlen(JSR_EXT)) {
                extension = dirent->d_name + (fileNameLen - strlen(JSR_EXT));
            }

            if(extension && strcmp(extension, opts.disassemble ? JSC_EXT : JSR_EXT) == 0) {
                allok &= compileDirFile(in, out, curr, dirent->d_name);
            }
            break;
        }
        default:
            // Ignore other file types
            break;
        }
    }

    if(closedir(directory)) {
        fprintf(stderr, "Cannot close dir %s: %s\n", curr->data, strerror(errno));
        return false;
    }

    return allok;
}

// Parse the app arguments into an Options struct
static void parseArguments(int argc, char** argv) {
    static const char* const usage[] = {
        "jstarc [options] <file>",
        "jstarc [options] <directory>",
        NULL,
    };

    static struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Options"),
        OPT_STRING('o', "output", &opts.output, "Output file or directory"),
        OPT_BOOLEAN('r', "recursive", &opts.recursive,
                    "Recursively compile/disassemble files in <directory>, does nothing if passed "
                    "argument is a <file>"),
        OPT_BOOLEAN('l', "list", &opts.list,
                    "List the compiled bytecode instead of saving it on file"),
        OPT_BOOLEAN('d', "disassemble", &opts.disassemble,
                    "Disassemble already compiled jsc files and list their content"),
        OPT_BOOLEAN('c', "compile-only", &opts.compileOnly,
                    "Compile files but do not generate output files. Used for syntax checking"),
        OPT_BOOLEAN('C', "no-colors", &opts.disableColors,
                    "Disable output coloring. Hints are disabled as well"),
        OPT_BOOLEAN('v', "version", &opts.showVersion, "Print version information and exit"),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "jstarc compiles J* source files to bytecode", NULL);
    int args = argparse_parse(&argparse, argc, (const char**)argv);

    // Bail out early if we only need to show the version
    if(opts.showVersion) {
        printVersion();
        exit(EXIT_SUCCESS);
    }

    if(args != 1) {
        argparse_usage(&argparse);
        exit(EXIT_FAILURE);
    }

    opts.input = argv[0];
}

// Init the app state by parsing arguments and initializing the J* vm
static void initApp(int argc, char** argv) {
    parseArguments(argc, argv);

    JStarConf conf = jsrGetConf();
    conf.errorCallback = &errorCallback;

    PROFILE_BEGIN_SESSION("jstar-init.json")
    vm = jsrNewVM(&conf);
    PROFILE_END_SESSION()
}

// Free the app state
static void freeApp(void) {
    PROFILE_BEGIN_SESSION("jstar-free.json")
    jsrFreeVM(vm);
    PROFILE_END_SESSION()
}

int main(int argc, char** argv) {
    initApp(argc, argv);
    atexit(&freeApp);

    bool directoryCompile = isDirectory(opts.input);

    // Create input path
    Path inputPath = pathNew();
    pathAppendStr(&inputPath, opts.input);
    pathNormalize(&inputPath);

    // Create ouput path (copy input and change extension if no output path is provided)
    Path outputPath = pathNew();
    if(opts.output) {
        pathAppendStr(&outputPath, opts.output);
        pathNormalize(&outputPath);
    } else {
        outputPath = pathCopy(&inputPath);
        if(!directoryCompile) {
            pathChangeExtension(&outputPath, JSC_EXT);
        }
    }

    PROFILE_BEGIN_SESSION("jstar-run.json")

    bool ok;
    if(directoryCompile) {
        ok = compileDirectory(&inputPath, &outputPath, &inputPath);
    } else if(opts.disassemble) {
        ok = disassembleFile(&inputPath);
    } else {
        ok = compileFile(&inputPath, &outputPath);
    }

    PROFILE_END_SESSION()

    pathFree(&inputPath);
    pathFree(&outputPath);

    exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
