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
#include "hints.h"
#include "import.h"
#include "jstar/jstar.h"
#include "jstar/parse/ast.h"
#include "jstar/parse/lex.h"
#include "jstar/parse/parser.h"
#include "profiler.h"

#define JSTAR_PROMPT (opts.disableColors ? "J*>> " : "\033[0;1;97mJ*>> \033[0m")
#define LINE_PROMPT  (opts.disableColors ? ".... " : "\033[0;1;97m.... \033[0m")
#define REPL_PRINT   "__replprint"
#define INDENT       "    "

JSR_STATIC_ASSERT(TOK_EOF == 78, "Token count has changed, update tokenDepth if needed");
// Token depth table for tracking the number of open blocks in the REPL.
static const int tokenDepth[TOK_EOF] = {
    // Tokens that start a new block
    [TOK_LSQUARE] = 1,
    [TOK_LCURLY] = 1,
    [TOK_BEGIN] = 1,
    [TOK_CLASS] = 1,
    [TOK_WHILE] = 1,
    [TOK_WITH] = 1,
    [TOK_CTOR] = 1,
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
    bool disableColors;
    bool disableHints;
    char* execStmt;
    char** args;
    int argsCount;
} Options;

// -----------------------------------------------------------------------------
// APP STATE
// -----------------------------------------------------------------------------

static Options opts;
static JStarVM* vm;
static JStarBuffer completionBuf;
static Replxx* replxx;

// -----------------------------------------------------------------------------
// CALLBACKS AND HOOKS
// -----------------------------------------------------------------------------

// J* error callback that prints colored error messages
static void errorCallback(JStarVM* vm, JStarResult res, const char* file, int ln, const char* err) {
    PROFILE_FUNC()
    if(ln >= 0) {
        fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "File %s [line:%d]:\n", file, ln);
    } else {
        fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "File %s:\n", file);
    }
    fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "%s\n", err);
}

// Replxx autocompletion hook with indentation support
static void indent(const char* input, replxx_completions* completions, int* ctxLen, void* ud) {
    Replxx* replxx = ud;
    jsrBufferClear(&completionBuf);

    ReplxxState state;
    replxx_get_state(replxx, &state);

    int cursorPos = state.cursorPosition;
    int inputLen = strlen(input);
    int indentLen = strlen(INDENT);

    // Indent the current context up to a multiple of strlen(INDENT)
    jsrBufferAppendf(&completionBuf, "%.*s", *ctxLen, input + inputLen - *ctxLen);
    jsrBufferAppendf(&completionBuf, "%.*s", indentLen - (cursorPos % indentLen), INDENT);

    // Give the processed output to replxx for visualization
    replxx_add_completion(completions, completionBuf.data);
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

// Print the J* version along with its compilation environment.
static void printVersion(void) {
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);
}

// SIGINT handler to break evaluation on CTRL-C.
static void sigintHandler(int sig) {
    signal(sig, SIG_DFL);
    jsrEvalBreak(vm);
}

// Wrapper function to evaluate source or binary J* code.
// Sets up a signal handler to support the breaking of evaluation using CTRL-C.
static JStarResult evaluate(const char* path, const JStarBuffer* code) {
    signal(SIGINT, &sigintHandler);
    JStarResult res = jsrEval(vm, path, code->data, code->size);
    signal(SIGINT, SIG_DFL);
    return res;
}

// Wrapper function to evaluate J* source code passed in as a c-string.
// Sets up a signal handler to support the breaking of evaluation using CTRL-C.
static JStarResult evaluateString(const char* path, const char* src) {
    signal(SIGINT, &sigintHandler);
    JStarResult res = jsrEvalString(vm, path, src);
    signal(SIGINT, SIG_DFL);
    return res;
}

// -----------------------------------------------------------------------------
// SCRIPT EXECUTION
// -----------------------------------------------------------------------------

// Execute a J* source or compiled file from disk.
static JStarResult execScript(const char* script, int argc, char** args) {
    PROFILE_BEGIN_SESSION("jstar-run.json")

    JStarResult res;
    {
        PROFILE_FUNC()

        JStarBuffer code;
        if(!jsrReadFile(vm, script, &code)) {
            fConsolePrint(replxx, REPLXX_STDERR, COLOR_RED, "Error reading script '%s': %s\n",
                          script, strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Execute the script; make sure to use the absolute path for consistency
        jsrInitCommandLineArgs(vm, argc, (const char**)args);
        res = evaluate(script, &code);

        jsrBufferFree(&code);
    }

    PROFILE_END_SESSION()
    return res;
}

// -----------------------------------------------------------------------------
// REPL
// -----------------------------------------------------------------------------

// Counts the number of blocks in a single line of J* code.
// Used to handle multiline input in the repl.
static int countBlocks(const char* line) {
    PROFILE_FUNC()

    JStarLex lex;
    JStarTok tok;

    jsrInitLexer(&lex, line, strlen(line));
    jsrNextToken(&lex, &tok);

    if(!tokenDepth[tok.type]) return 0;

    int depth = 0;
    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
        depth += tokenDepth[tok.type];
        jsrNextToken(&lex, &tok);
    }

    return depth;
}

// J* native function that formats and colors the output depending on the Value's type.
static bool replPrint(JStarVM* vm) {
    // Don't print `null`
    if(jsrIsNull(vm, 1)) return true;

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
        consolePrint(replxx, COLOR_WHITE, "%s\n", jsrGetString(vm, -1));
    }

    jsrPushNull(vm);
    return true;
}

// Register the custom `replPrint` function in the __main__ module.
static void registerPrintFunction(void) {
    jsrPushNative(vm, JSR_MAIN_MODULE, REPL_PRINT, &replPrint, 1);
    jsrSetGlobal(vm, JSR_MAIN_MODULE, REPL_PRINT);
    jsrPop(vm);
}

// Add an additional print statement if the current input is a valid J* expression.
// Also, the current expression is assigned to `_` in order to permit calculation chaining.
static void addReplPrint(JStarBuffer* sb) {
    PROFILE_FUNC()
    JStarExpr* e = jsrParseExpression("<repl>", sb->data, sb->size, NULL, NULL);
    if(e != NULL) {
        jsrBufferPrependStr(sb, "var _ = ");
        jsrBufferAppendf(sb, ";%s(_)", REPL_PRINT);
        jsrExprFree(e);
    }
}

// The interactive read-eval-print loop.
static JStarResult doRepl(void) {
    PROFILE_BEGIN_SESSION("jstar-repl.json")

    JStarResult res = JSR_SUCCESS;
    {
        PROFILE_FUNC()

        registerPrintFunction();

        if(!opts.skipVersion) {
            printVersion();
        }

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
    return res;
}

// -----------------------------------------------------------------------------
// APP INITIALIZATION AND MAIN FUNCTION
// -----------------------------------------------------------------------------

// Parse the app arguments into an Option struct
static void parseArguments(int argc, char** argv) {
    static const char* const usage[] = {
        "jstar [options] [script [arguments...]]",
        NULL,
    };

    static struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Options"),
        OPT_BOOLEAN('V', "skip-version", &opts.skipVersion, "Don't print version information when entering the REPL"),
        OPT_STRING('e', "exec", &opts.execStmt, "Execute the given statement. If 'script' is provided it is executed after this"),
        OPT_BOOLEAN('i', "interactive", &opts.interactive, "Enter the REPL after executing 'script' and/or '-e' statement"),
        OPT_BOOLEAN('E', "ignore-env", &opts.ignoreEnv, "Ignore environment variables such as JSTARPATH"),
        OPT_BOOLEAN('C', "no-colors", &opts.disableColors, "Disable output coloring. Hints are disabled as well"),
        OPT_BOOLEAN('H', "no-hints", &opts.disableHints, "Disable hinting support"),
        OPT_BOOLEAN('v', "version", &opts.showVersion, "Print version information and exit", 0),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, ARGPARSE_STOP_AT_NON_OPTION);
    argparse_describe(&argparse, "J* a lightweight scripting language", NULL);
    int args = argparse_parse(&argparse, argc, (const char**)argv);

    // Bail out early if we only need to show the version
    if(opts.showVersion) {
        printVersion();
        exit(EXIT_SUCCESS);
    }

    if(args > 0) {
        opts.script = argv[0];
    }

    if(args > 1) {
        opts.args = &argv[1];
        opts.argsCount = args - 1;
    }
}

// Init the app state by parsing arguments and initializing J* and replxx
static void initApp(int argc, char** argv) {
    parseArguments(argc, argv);

    JStarConf conf = jsrGetConf();
    conf.errorCallback = &errorCallback;
    conf.importCallback = &importCallback;

    // Init the J* VM
    PROFILE_BEGIN_SESSION("jstar-init.json")

    vm = jsrNewVM(&conf);
    jsrInitRuntime(vm);

    initImports(vm, opts.script, opts.ignoreEnv);
    jsrBufferInit(vm, &completionBuf);

    PROFILE_END_SESSION()

    // Init replxx for repl and output coloring/hints support
    replxx = replxx_init();
    replxx_set_completion_callback(replxx, &indent, replxx);
    replxx_set_no_color(replxx, opts.disableColors);
    if(!opts.disableColors) replxx_set_highlighter_callback(replxx, &highlighter, replxx);
    if(!opts.disableColors && !opts.disableHints) replxx_set_hint_callback(replxx, &hints, vm);
}

// Free the app state
static void freeApp(void) {
    jsrBufferFree(&completionBuf);

    // Free  the J* VM
    PROFILE_BEGIN_SESSION("jstar-free.json")
    jsrFreeVM(vm);
    PROFILE_END_SESSION()

    freeImports();

    // Free replxx
    replxx_history_clear(replxx);
    replxx_end(replxx);
}

int main(int argc, char** argv) {
    initApp(argc, argv);
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

    exit(doRepl());
}
