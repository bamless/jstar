#include <argparse.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extlib.h"
#include "jstar/buffer.h"
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
        if(!opts.disableColors && isatty(fileno(stderr))) {
            fprintf(stderr, COLOR_RED);
        }
        fprintf(stderr, "%s:%d:%d: error\n", file, loc.line, loc.col);
        fprintf(stderr, "%s\n", err);
        if(!opts.disableColors && isatty(fileno(stderr))) {
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
        if(!write_file(out->data, compiled.data, compiled.size)) {
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
    Paths files = {0};
    if(!read_dir(curr->data, &files)) return false;

    bool allok = true;
    array_foreach(char*, it, &files) {
        const char* file = *it;
        Path abs_path = pathCopy(curr);
        pathJoinStr(&abs_path, file);

        FileType t = get_file_type(abs_path.data);
        if(t == FILE_ERR) {
            allok = false;
            continue;
        }

        switch(t) {
        case FILE_DIR: {
            if(opts.recursive) {
                allok &= compileDirectory(in, out, &abs_path);
            }
            break;
        }
        case FILE_REGULAR: {
            size_t fileNameLen = strlen(file);
            const char* extension = NULL;
            if(fileNameLen > strlen(JSR_EXT)) {
                extension = file + (fileNameLen - strlen(JSR_EXT));
            }
            if(extension && strcmp(extension, opts.disassemble ? JSC_EXT : JSR_EXT) == 0) {
                allok &= compileDirFile(in, out, curr, file);
            }
            break;
        }
        default:
            // Ignore other file types
            break;
        }

        pathFree(&abs_path);
    }

    free_paths(&files);
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

    FileType input_type = get_file_type(opts.input);
    if(input_type == FILE_ERR) return 1;

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
        if(input_type != FILE_DIR) {
            pathChangeExtension(&outputPath, JSC_EXT);
        }
    }

    PROFILE_BEGIN_SESSION("jstar-run.json")

    bool ok;
    if(input_type == FILE_DIR) {
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
