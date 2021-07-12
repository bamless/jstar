#include <argparse.h>
#include <errno.h>
#include <replxx.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_print.h"
#include "highlighter.h"
#include "jstar/jstar.h"
#include "jstar/parse/ast.h"
#include "jstar/parse/lex.h"
#include "jstar/parse/parser.h"
#include "profiler.h"

#define REPL_PRINT   "__replprint"
#define JSTAR_PATH   "JSTARPATH"
#define JSTAR_PROMPT "J*>> "
#define LINE_PROMPT  ".... "
#define INDENT       "    "

static const int tokenDepth[TOK_EOF] = {
    // Tokens that start a new block
    [TOK_LSQUARE] = 1,
    [TOK_LCURLY] = 1,
    [TOK_BEGIN] = 1,
    [TOK_CLASS] = 1,
    [TOK_WHILE] = 1,
    [TOK_WITH] = 1,
    [TOK_FUN] = 1,
    [TOK_TRY] = 1,
    [TOK_FOR] = 1,
    [TOK_IF] = 1,

    // Tokens that end a block
    [TOK_RSQUARE] = -1,
    [TOK_RCURLY] = -1,
    [TOK_END] = -1,
};

typedef struct Options {
    char* script;
    bool showVersion;
    bool skipVersion;
    bool interactive;
    bool ignoreEnv;
    char* execStmt;
    char** args;
    int argsCount;
} Options;

static Options opts;
static JStarVM* vm;
static JStarBuffer completionBuf;
static Replxx* replxx;

// -----------------------------------------------------------------------------
// VM INITIALIZATION AND DESTRUCTION
// -----------------------------------------------------------------------------

static void errorCallback(JStarVM* vm, JStarResult res, const char* file, int ln, const char* err) {
    PROFILE_FUNC()
    if(ln >= 0) {
        fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "File %s [line:%d]:\n", file, ln);
    } else {
        fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "File %s:\n", file);
    }
    fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "%s\n", err);
}

static bool replPrint(JStarVM* vm) {
    if(jsrIsNull(vm, 1)) return true;  // Don't print `null`

    jsrDup(vm);
    bool isString = jsrIsString(vm, 1);
    if(jsrCallMethod(vm, isString ? "escaped" : "__string__", 0) != JSR_SUCCESS) return false;
    JSR_CHECK(String, -1, "Cannot convert result to String");

    if(jsrIsString(vm, 1)) {
        consolePrint(replxx, COLOR_BLUE, "\"%s\"\n", jsrGetString(vm, -1));
    } else if(jsrIsNumber(vm, 1)) {
        consolePrint(replxx, COLOR_GREEN, "%s\n", jsrGetString(vm, -1));
    } else if(jsrIsBoolean(vm, 1)) {
        consolePrint(replxx, COLOR_CYAN, "%s\n", jsrGetString(vm, -1));
    } else {
        printf("%s\n", jsrGetString(vm, -1));
    }

    jsrPushNull(vm);
    return true;
}

// Autocompletion with indentation support
static void completion(const char* input, replxx_completions* completions, int* ctxLen, void* ud) {
    Replxx* replxx = ud;
    jsrBufferClear(&completionBuf);

    ReplxxState state;
    replxx_get_state(replxx, &state);

    int cursorPos = state.cursorPosition;
    int inputLen = strlen(input);
    int indentLen = strlen(INDENT);

    // Insert the current contex back into the buffer
    jsrBufferAppendf(&completionBuf, "%.*s", *ctxLen, input + inputLen - *ctxLen);
    // Insert spaces aligning them on multiples of strlen(INDENT)
    jsrBufferAppendf(&completionBuf, "%.*s", indentLen - (cursorPos % indentLen), INDENT);
    
    // Give the processed output to replxx for visualization
    replxx_add_completion(completions, completionBuf.data);
}

static void initApp(void) {
    PROFILE_BEGIN_SESSION("jstar-init.json")

    // Init VM
    JStarConf conf = jsrGetConf();
    conf.errorCallback = &errorCallback;
    vm = jsrNewVM(&conf);
    jsrBufferInit(vm, &completionBuf);

    // Init replxx
    replxx = replxx_init();
    replxx_set_completion_callback(replxx, &completion, replxx);
    replxx_set_highlighter_callback(replxx, &highlighter, replxx);

    PROFILE_END_SESSION()
}

static void freeApp(void) {
    PROFILE_BEGIN_SESSION("jstar-free.json")

    // Free VM
    jsrBufferFree(&completionBuf);
    jsrFreeVM(vm);

    // Free replxx
    replxx_history_clear(replxx);
    replxx_end(replxx);

    PROFILE_END_SESSION()
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static void initImportPaths(const char* path) {
    jsrAddImportPath(vm, path);
    if(opts.ignoreEnv) return;

    const char* jstarPath = getenv(JSTAR_PATH);
    if(jstarPath == NULL) return;

    JStarBuffer buf;
    jsrBufferInit(vm, &buf);

    size_t last = 0;
    size_t pathLen = strlen(jstarPath);
    for(size_t i = 0; i < pathLen; i++) {
        if(jstarPath[i] == ':') {
            jsrBufferAppend(&buf, jstarPath + last, i - last);
            jsrAddImportPath(vm, buf.data);
            jsrBufferClear(&buf);
            last = i + 1;
        }
    }

    jsrBufferAppend(&buf, jstarPath + last, pathLen - last);
    jsrAddImportPath(vm, buf.data);
    jsrBufferFree(&buf);
}

static void sigintHandler(int sig) {
    signal(sig, SIG_DFL);
    jsrEvalBreak(vm);
}

static JStarResult evaluate(const char* name, const JStarBuffer* src) {
    signal(SIGINT, &sigintHandler);
    JStarResult res = jsrEval(vm, name, src);
    signal(SIGINT, SIG_DFL);
    return res;
}

static JStarResult evaluateString(const char* name, const char* src) {
    signal(SIGINT, &sigintHandler);
    JStarResult res = jsrEvalString(vm, name, src);
    signal(SIGINT, SIG_DFL);
    return res;
}

// -----------------------------------------------------------------------------
// REPL
// -----------------------------------------------------------------------------

static void printVersion(void) {
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);
}

static int countBlocks(const char* line) {
    PROFILE_FUNC()

    JStarLex lex;
    JStarTok tok;

    jsrInitLexer(&lex, line);
    jsrNextToken(&lex, &tok);

    if(!tokenDepth[tok.type]) return 0;

    int depth = 0;
    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
        depth += tokenDepth[tok.type];
        jsrNextToken(&lex, &tok);
    }

    return depth;
}

static void addReplPrint(JStarBuffer* sb) {
    PROFILE_FUNC()
    JStarExpr* e = jsrParseExpression("<repl>", sb->data, NULL, NULL);
    if(e != NULL) {
        jsrBufferPrependStr(sb, "var _ = ");
        jsrBufferAppendf(sb, ";%s(_)", REPL_PRINT);
        jsrExprFree(e);
    }
}

// register repl print function
static void registerPrintFunction(void) {
    jsrPushNative(vm, JSR_MAIN_MODULE, REPL_PRINT, &replPrint, 1);
    jsrSetGlobal(vm, JSR_MAIN_MODULE, REPL_PRINT);
    jsrPop(vm);
}

static void doRepl() {
    PROFILE_BEGIN_SESSION("jstar-repl.json")

    JStarResult res = JSR_SUCCESS;
    {
        PROFILE_FUNC()

        if(!opts.skipVersion) printVersion();
        initImportPaths("./");
        registerPrintFunction();

        JStarBuffer src;
        jsrBufferInit(vm, &src);

        const char* line;
        while((line = replxx_input(replxx, JSTAR_PROMPT)) != NULL) {
            int depth = countBlocks(line);
            replxx_history_add(replxx, line);
            jsrBufferAppendStr(&src, line);

            while(depth > 0 && (line = replxx_input(replxx, LINE_PROMPT)) != NULL) {
                depth += countBlocks(line);
                replxx_history_add(replxx, line);
                jsrBufferAppendChar(&src, '\n');
                jsrBufferAppendStr(&src, line);
            }

            addReplPrint(&src);

            res = evaluateString("<stdin>", src.data);
            jsrBufferClear(&src);
        }

        jsrBufferFree(&src);
    }

    PROFILE_END_SESSION()

    exit(res);
}

// -----------------------------------------------------------------------------
// SCRIPT EXECUTION
// -----------------------------------------------------------------------------

static JStarResult execScript(const char* script, int argc, char** args) {
    PROFILE_BEGIN_SESSION("jstar-run.json")

    JStarResult res;
    {
        PROFILE_FUNC()

        jsrInitCommandLineArgs(vm, argc, (const char**)args);

        // set base import path to script's directory
        char* directory = strrchr(script, '/');
        if(directory != NULL) {
            size_t length = directory - script + 1;
            char* path = calloc(length + 1, 1);
            memcpy(path, script, length);
            initImportPaths(path);
            free(path);
        } else {
            initImportPaths("./");
        }

        JStarBuffer src;
        if(!jsrReadFile(vm, script, &src)) {
            fprintf(stderr, "Error reading script '%s': %s\n", script, strerror(errno));
            exit(EXIT_FAILURE);
        }

        res = evaluate(script, &src);
        jsrBufferFree(&src);
    }

    PROFILE_END_SESSION()

    return res;
}

// -----------------------------------------------------------------------------
// MAIN FUNCTION AND ARGUMENT PARSE
// -----------------------------------------------------------------------------

static void parseArguments(int argc, char** argv) {
    opts = (Options){0};

    static const char* const usage[] = {
        "jstar [options] [script [arguments]]",
        NULL,
    };

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Options"),
        OPT_BOOLEAN('v', "version", &opts.showVersion, "Print version information and exit", 0, 0,
                    0),
        OPT_BOOLEAN('V', "skip-version", &opts.skipVersion,
                    "Don't print version information when entering the REPL", 0, 0, 0),
        OPT_STRING('e', "exec", &opts.execStmt,
                   "Execute the given statement. If 'script' is provided it is executed after this",
                   0, 0, 0),
        OPT_BOOLEAN('i', "interactive", &opts.interactive,
                    "Enter the REPL after executing 'script' and/or '-e' statement", 0, 0, 0),
        OPT_BOOLEAN('E', "ignore-env", &opts.ignoreEnv,
                    "Ignore environment variables such as JSTARPATH", 0, 0, 0),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, ARGPARSE_STOP_AT_NON_OPTION);
    argparse_describe(&argparse, "J* a lightweight scripting language", NULL);
    int nonOpts = argparse_parse(&argparse, argc, (const char**)argv);

    if(nonOpts > 0) {
        opts.script = argv[0];
    }

    if(nonOpts > 1) {
        opts.args = &argv[1];
        opts.argsCount = nonOpts - 1;
    }
}

int main(int argc, char** argv) {
    parseArguments(argc, argv);

    if(opts.showVersion) {
        printVersion();
        exit(EXIT_SUCCESS);
    }

    initApp();
    atexit(&freeApp);

    if(opts.execStmt) {
        JStarResult res = evaluateString("<string>", opts.execStmt);
        if(opts.script) {
            res = execScript(opts.script, opts.argsCount, opts.args);
        }
        if(!opts.interactive) exit(res);
    } else if(opts.script) {
        JStarResult res = execScript(opts.script, opts.argsCount, opts.args);
        if(!opts.interactive) exit(res);
    }

    doRepl();
}
