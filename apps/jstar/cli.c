#include <argparse.h>
#include <errno.h>
#include <linenoise.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorio.h"
#include "jstar/jstar.h"
#include "jstar/parse/ast.h"
#include "jstar/parse/lex.h"
#include "jstar/parse/parser.h"

#define REPL_PRINT   "_replprint"
#define JSTAR_PATH   "JSTARPATH"
#define JSTAR_PROMPT "J*>> "
#define LINE_PROMPT  ".... "

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

// -----------------------------------------------------------------------------
// VM INITIALIZATION AND DESTRUCTION
// -----------------------------------------------------------------------------

static void errorCallback(JStarVM* vm, JStarResult res, const char* file, int ln, const char* err) {
    if(ln >= 0) {
        fcolorPrintf(stderr, COLOR_RED, "File %s [line:%d]:\n", file, ln);
    } else {
        fcolorPrintf(stderr, COLOR_RED, "File %s:\n", file);
    }
    fcolorPrintf(stderr, COLOR_RED, "%s\n", err);
}

static bool replPrint(JStarVM* vm) {
    jsrDup(vm);
    if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) return false;
    JSR_CHECK(String, -1, "__string__ return value");

    if(jsrIsString(vm, 1)) {
        colorPrintf(COLOR_GREEN, "\"%s\"\n", jsrGetString(vm, -1));
    } else if(jsrIsNumber(vm, 1)) {
        colorPrintf(COLOR_MAGENTA, "%s\n", jsrGetString(vm, -1));
    } else if(jsrIsBoolean(vm, 1)) {
        colorPrintf(COLOR_CYAN, "%s\n", jsrGetString(vm, -1));
    } else {
        printf("%s\n", jsrGetString(vm, -1));
    }

    jsrPushNull(vm);
    return true;
}

static void initVM(void) {
    JStarConf conf = jsrGetConf();
    conf.errorCallback = &errorCallback;
    vm = jsrNewVM(&conf);
    jsrBufferInit(vm, &completionBuf);

    // register repl print function
    jsrPushNative(vm, JSR_MAIN_MODULE, REPL_PRINT, &replPrint, 1);
    jsrSetGlobal(vm, JSR_MAIN_MODULE, REPL_PRINT);
    jsrPop(vm);
}

static void freeVM(void) {
    jsrBufferFree(&completionBuf);
    jsrFreeVM(vm);
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static void initImportPaths(const char* path, bool ignoreEnv) {
    jsrAddImportPath(vm, path);
    if(ignoreEnv) return;

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

// Little hack to enable adding a tab in the REPL.
// Simply add 4 spaces on linenoise tab completion.
static void completion(const char* buf, linenoiseCompletions* lc) {
    jsrBufferClear(&completionBuf);
    jsrBufferAppendf(&completionBuf, "%s    ", buf);
    linenoiseAddCompletion(lc, completionBuf.data);
}

static int countBlocks(const char* line) {
    JStarLex lex;
    JStarTok tok;

    jsrInitLexer(&lex, line);
    jsrNextToken(&lex, &tok);

    int depth = 0;
    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
        switch(tok.type) {
        case TOK_LSQUARE:
        case TOK_LCURLY:
        case TOK_BEGIN:
        case TOK_CLASS:
        case TOK_THEN:
        case TOK_WITH:
        case TOK_FUN:
        case TOK_TRY:
        case TOK_DO:
            depth++;
            break;
        case TOK_RSQUARE:
        case TOK_RCURLY:
        case TOK_ELIF:
        case TOK_END:
            depth--;
            break;
        default:
            break;
        }
        jsrNextToken(&lex, &tok);
    }
    return depth;
}

static void addExprPrint(JStarBuffer* sb) {
    JStarExpr* e = jsrParseExpression("<repl>", sb->data, NULL, NULL);
    if(e != NULL) {
        jsrBufferPrependStr(sb, "var _ = ");
        jsrBufferAppendStr(sb, "\nif _ != null then _replprint(_) end");
        jsrExprFree(e);
    }
}

static void doRepl() {
    if(!opts.skipVersion) printVersion();
    linenoiseSetCompletionCallback(completion);
    initImportPaths("./", opts.ignoreEnv);

    JStarBuffer src;
    jsrBufferInit(vm, &src);
    JStarResult res = JSR_SUCCESS;

    char* line;
    while((line = linenoise(JSTAR_PROMPT)) != NULL) {
        int depth = countBlocks(line);
        linenoiseHistoryAdd(line);
        jsrBufferAppendStr(&src, line);
        free(line);

        while(depth > 0 && (line = linenoise(LINE_PROMPT)) != NULL) {
            depth += countBlocks(line);
            linenoiseHistoryAdd(line);
            jsrBufferAppendChar(&src, '\n');
            jsrBufferAppendStr(&src, line);
            free(line);
        }

        addExprPrint(&src);
        res = evaluateString("<stdin>", src.data);
        jsrBufferClear(&src);
    }

    jsrBufferFree(&src);
    linenoiseHistoryFree();
    exit(res);
}

// -----------------------------------------------------------------------------
// SCRIPT EXECUTION
// -----------------------------------------------------------------------------

static JStarResult execScript(const char* script, int argc, char** args, bool ignoreEnv) {
    jsrInitCommandLineArgs(vm, argc, (const char**)args);

    // set base import path to script's directory
    char* directory = strrchr(script, '/');
    if(directory != NULL) {
        size_t length = directory - script + 1;
        char* path = calloc(length + 1, sizeof(char));
        memcpy(path, script, length);
        initImportPaths(path, ignoreEnv);
        free(path);
    } else {
        initImportPaths("./", ignoreEnv);
    }

    JStarBuffer src;
    if(!jsrReadFile(vm, script, &src)) {
        fprintf(stderr, "Error reading script '%s': %s\n", script, strerror(errno));
        exit(EXIT_FAILURE);
    }

    JStarResult res = evaluate(script, &src);
    jsrBufferFree(&src);

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

    initVM();
    atexit(&freeVM);

    if(opts.execStmt) {
        JStarResult res = evaluateString("<string>", opts.execStmt);
        if(opts.script) {
            res = execScript(opts.script, opts.argsCount, opts.args, opts.ignoreEnv);
        }
        if(!opts.interactive) exit(res);
    } else if(opts.script) {
        JStarResult res = execScript(opts.script, opts.argsCount, opts.args, opts.ignoreEnv);
        if(!opts.interactive) exit(res);
    }

    doRepl();
}
