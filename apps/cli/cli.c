#include <argparse.h>
#include <linenoise.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"
#include "jstar/parse/ast.h"
#include "jstar/parse/lex.h"
#include "jstar/parse/parser.h"

#define JSTAR_PATH   "JSTARPATH"
#define JSTAR_PROMPT "J*>> "
#define LINE_PROMPT  ".... "

static JStarVM* vm;
static JStarBuffer completionBuf;

typedef struct Options {
    const char* script;
    bool showVersion, skipVersion, interactive, ignoreEnv;
    char* execStmt;
    const char** args;
    int argsCount;
} Options;

static void initVM(void) {
    JStarConf conf = jsrGetConf();
    vm = jsrNewVM(&conf);
    jsrBufferInit(vm, &completionBuf);
}

static void exitFree(int code) {
    jsrBufferFree(&completionBuf);
    jsrFreeVM(vm);
    exit(code);
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

static JStarResult evaluate(const char* name, const char* src) {
    signal(SIGINT, &sigintHandler);
    JStarResult res = jsrEvaluate(vm, name, src);
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
    JStarExpr* e = jsrParseExpression("<repl>", sb->data, NULL);
    if(e != NULL) {
        jsrBufferPrependStr(sb, "var _ = ");
        jsrBufferAppendStr(sb, "\nif _ != null then print(_) end");
        jsrExprFree(e);
    }
}

static void doRepl(Options* opts) {
    if(!opts->skipVersion) printVersion();
    linenoiseSetCompletionCallback(completion);
    initImportPaths("./", opts->ignoreEnv);

    JStarBuffer src;
    jsrBufferInit(vm, &src);
    JStarResult res = JSR_EVAL_SUCCESS;

    char* line;
    while((line = linenoise(JSTAR_PROMPT)) != NULL) {
        linenoiseHistoryAdd(line);
        int depth = countBlocks(line);
        jsrBufferAppendStr(&src, line);
        free(line);

        while(depth > 0 && (line = linenoise(LINE_PROMPT)) != NULL) {
            linenoiseHistoryAdd(line);
            depth += countBlocks(line);
            jsrBufferAppendChar(&src, '\n');
            jsrBufferAppendStr(&src, line);
            free(line);
        }

        addExprPrint(&src);
        res = evaluate("<stdin>", src.data);
        jsrBufferClear(&src);
    }

    jsrBufferFree(&src);
    linenoiseHistoryFree();
    exitFree(res);
}

// -----------------------------------------------------------------------------
// SCRIPT EXECUTION
// -----------------------------------------------------------------------------

static JStarResult execScript(const char* script, int argc, const char** args, bool ignoreEnv) {
    jsrInitCommandLineArgs(vm, argc, args);

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

    char* src = jsrReadFile(script);
    if(src == NULL) {
        fprintf(stderr, "Error reading script ");
        perror(script);
        exitFree(EXIT_FAILURE);
    }

    JStarResult res = evaluate(script, src);
    free(src);

    return res;
}

// -----------------------------------------------------------------------------
// MAIN FUNCTION AND ARGUMENT PARSE
// -----------------------------------------------------------------------------

static Options parseArguments(int argc, const char** argv) {
    Options opts = {0};

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
    int nonOpts = argparse_parse(&argparse, argc, argv);

    if(nonOpts > 0) {
        opts.script = argv[0];
    }

    if(nonOpts > 1) {
        opts.args = &argv[1];
        opts.argsCount = nonOpts - 1;
    }

    return opts;
}

int main(int argc, const char** argv) {
    Options opts = parseArguments(argc, argv);

    if(opts.showVersion) {
        printVersion();
        exit(EXIT_SUCCESS);
    }

    initVM();

    if(opts.execStmt) {
        JStarResult res = jsrEvaluate(vm, "<string>", opts.execStmt);
        if(opts.script) {
            res = execScript(opts.script, opts.argsCount, opts.args, opts.ignoreEnv);
        }
        if(!opts.interactive) exitFree(res);
    } else if(opts.script) {
        JStarResult res = execScript(opts.script, opts.argsCount, opts.args, opts.ignoreEnv);
        if(!opts.interactive) exitFree(res);
    }

    doRepl(&opts);
}
