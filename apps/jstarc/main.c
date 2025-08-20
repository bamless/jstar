#include <argparse.h>
#include <asm-generic/errno-base.h>
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

    StringBuffer sb = {0};
    if(!read_file(path->data, &sb)) return false;
    sb_append_char(&sb, '\0');

    ext_log(INFO, "compiling %s to %s", path->data, out->data);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path->data, sb.items, &compiled);
    sb_free(&sb);

    if(res != JSR_SUCCESS) {
        ext_log(ERROR, "couldn't compile file %s", path->data);
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

    StringBuffer sb = {0};
    if(!read_file(path->data, &sb)) return false;
    ext_log(INFO, "disassembling %s", path->data);

    JStarResult res = jsrDisassembleCode(vm, path->data, sb.items, sb.size);
    sb_free(&sb);

    if(res != JSR_SUCCESS) {
        ext_log(ERROR, "couldn't disassemble file %s", path->data);
        return false;
    }

    return true;
}

// Creates the output directory path using the input root directory, output root directory and
// the current position in the directory tree.
static Path makeOutPath(const Path* in, const Path* out, const Path* curr) {
    Path outPath = pathCopy(out);
    size_t commonPath = pathIntersectOffset(in, curr);
    if(commonPath) {
        pathJoinStr(&outPath, curr->data + commonPath);
    } else if(strcmp(curr->data, ".") != 0) {
        pathJoinStr(&outPath, curr->data);
    }
    return outPath;
}

// Walk a directory (recursively, if `-r` was specified) and process all files that end in a `.jsr`
// or `.jsc` extension. Returns true on success, false on failure.
static bool compileDirectory(const Path* in, const Path* out, const Path* curr) {
    Paths files = {0};
    if(!read_dir(curr->data, &files)) return false;

    Path outPath = makeOutPath(in, out, curr);
    ext_context->log_level = NO_LOGGING;
    FileType dt = get_file_type(outPath.data);
    ext_context->log_level = INFO;
    if(dt == FILE_ERR && errno == ENOENT) {
        if(!create_dir(outPath.data)) {
            free_paths(&files);
            return false;
        }
    }

    bool allok = true;
    array_foreach(char*, it, &files) {
        const char* file = *it;
        Path filePath = pathCopy(curr);
        pathJoinStr(&filePath, file);

        switch(get_file_type(filePath.data)) {
        case FILE_DIR: {
            if(opts.recursive) {
                allok &= compileDirectory(in, out, &filePath);
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
                if(!opts.disassemble) {
                    Path outFile = pathCopy(&outPath);
                    pathJoinStr(&outFile, file);
                    pathChangeExtension(&outFile, JSC_EXT);
                    allok &= compileFile(&filePath, &outFile);
                    pathFree(&outFile);
                } else {
                    allok &= disassembleFile(&filePath);
                }
            }
            break;
        }
        default:
            // Ignore other file types
            break;
        }

        pathFree(&filePath);
    }

    free_paths(&files);
    pathFree(&outPath);
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
        OPT_BOOLEAN('C', "no-colors", &opts.disableColors, "Disable output coloring"),
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

    if((opts.compileOnly || opts.list || opts.disassemble) && opts.output) {
        ext_log(ERROR, "option `-o` cannot be used with `-c`, `-l` or `-d`");
        exit(EXIT_FAILURE);
    }

    if(args != 1) {
        ext_log(ERROR, "missing <file> or <directory> argument");
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

    Path inputPath = pathNew();
    pathAppendStr(&inputPath, opts.input);
    pathNormalize(&inputPath);

    Path outputPath = pathNew();
    if(opts.output) {
        pathAppendStr(&outputPath, opts.output);
        pathNormalize(&outputPath);
    } else {
        // Copy input path and change extension if no output path is provided
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
