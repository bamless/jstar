#include <argparse.h>
#include <errno.h>
#include <extlib.h>
#include <jstar/buffer.h>
#include <jstar/jstar.h>
#include <jstar/parse/lex.h>
#include <path.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef JSTAR_WINDOWS
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
    (void)vm;
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
    StringBuffer src = {0};
    if(!read_file(path->items, &src)) return false;

    printf("Compiling %s to %s\n", path->items, out->items);

    JStarBuffer compiled;
    JStarResult res = jsrCompileCode(vm, path->items, src.items, src.size, &compiled);
    sb_free(&src);

    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error compiling file '%s'\n", path->items);
        return false;
    }

    if(opts.list) {
        jsrDisassembleCode(vm, path->items, compiled.data, compiled.size);
    } else if(!opts.compileOnly) {
        if(!write_file(out->items, compiled.data, compiled.size)) {
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
    StringBuffer code = {0};
    if(!read_file(path->items, &code)) return false;

    printf("Disassembling %s\n", path->items);

    JStarResult res = jsrDisassembleCode(vm, path->items, code.items, code.size);
    sb_free(&code);

    if(res != JSR_SUCCESS) {
        fprintf(stderr, "Error disassembling file '%s'\n", path->items);
        return false;
    }

    return true;
}

// Walk a directory (recursively, if `-r` was specified) and process all files that end in a `.jsr`
// or `.jsc` extension. Returns true on success, false on failure.
static bool compileDirectory(const Path* in, const Path* out, const Path* curr) {
    bool res = true;
    Paths files = {0};
    Path outPath = pathNew(out->items, curr->items + pathIntersectOffset(in, curr));

    if(!read_dir(curr->items, &files)) return_exit(false);

    Context ctx = *ext_context;
    ctx.log_level = NO_LOGGING;
    push_context(&ctx);
    FileType dt = get_file_type(outPath.items);
    pop_context();
    if(dt == FILE_ERR && errno == ENOENT) {
        if(!create_dir(outPath.items)) return_exit(false);
    }

    array_foreach(char*, it, &files) {
        const char* file = *it;
        Path filePath = pathNew(curr->items, file);

        switch(get_file_type(filePath.items)) {
        case FILE_DIR: {
            if(opts.recursive) {
                res &= compileDirectory(in, out, &filePath);
            }
            break;
        }
        case FILE_REGULAR: {
            if(opts.disassemble) {
                if(ss_ends_with(SS(file), SS(JSC_EXT))) {
                    res &= disassembleFile(&filePath);
                }
            } else if(ss_ends_with(SS(file), SS(JSR_EXT))) {
                Path outFile = pathNew(outPath.items, file);
                pathChangeExtension(&outFile, JSC_EXT);
                res &= compileFile(&filePath, &outFile);
                pathFree(&outFile);
            }
            break;
        }
        default:
            // Ignore other file types
            break;
        }

        pathFree(&filePath);
    }

exit:
    free_paths(&files);
    pathFree(&outPath);
    return res;
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
        fprintf(stderr, "error: option `-o` cannot be used with `-c`, `-l` or `-d`\n");
        argparse_usage(&argparse);
        exit(EXIT_FAILURE);
    }

    if(args != 1) {
        fprintf(stderr, "missing <file> or <directory> argument\n");
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
    vm = jsrNewVM(&conf);
}

// Free the app state
static void freeApp(void) {
    jsrFreeVM(vm);
}

int main(int argc, char** argv) {
    initApp(argc, argv);
    atexit(&freeApp);

    FileType input_type = get_file_type(opts.input);
    if(input_type == FILE_ERR) return 1;

    Path inputPath = pathNew(opts.input);
    pathNormalize(&inputPath);

    Path outputPath;
    if(opts.output) {
        outputPath = pathNew(opts.output);
        pathNormalize(&outputPath);
    } else {
        // Copy input path and change extension if no output path is provided
        outputPath = pathNew(inputPath.items);
        if(input_type != FILE_DIR) {
            pathChangeExtension(&outputPath, JSC_EXT);
        }
    }

    bool res;
    if(input_type == FILE_DIR) {
        res = compileDirectory(&inputPath, &outputPath, &inputPath);
    } else if(opts.disassemble) {
        res = disassembleFile(&inputPath);
    } else {
        res = compileFile(&inputPath, &outputPath);
    }

    pathFree(&inputPath);
    pathFree(&outputPath);
    exit(res ? EXIT_SUCCESS : EXIT_FAILURE);
}
