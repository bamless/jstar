#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"
#include "jsrparse/ast.h"
#include "jsrparse/lex.h"
#include "jsrparse/parser.h"
#include "jsrparse/token.h"
#include "jstar.h"
#include "linenoise.h"

#define JSTARPATH "JSTARPATH"

static JStarVM* vm;

// ---- REPL implementation ----

static void printVersion() {
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);
}

// Little hack to enable adding a tab in linenoise
static void completion(const char* buf, linenoiseCompletions* lc) {
    char indented[1024];
    snprintf(indented, sizeof(indented), "%s    ", buf);
    linenoiseAddCompletion(lc, indented);
}

static void initImportPaths(const char* path, bool ignoreEnv) {
    jsrAddImportPath(vm, path);
    if(!ignoreEnv) {
        const char* jstarPath = getenv(JSTARPATH);
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
}

static int countBlocks(const char* line) {
    Lexer lex;
    Token tok;

    initLexer(&lex, line);
    nextToken(&lex, &tok);

    int depth = 0;
    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
        switch(tok.type) {
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
        case TOK_RCURLY:
        case TOK_ELIF:
        case TOK_END:
            depth--;
            break;
        default:
            break;
        }
        nextToken(&lex, &tok);
    }
    return depth;
}

static void addPrintIfExpr(JStarBuffer* sb) {
    Expr* e = parseExpression("<repl>", sb->data, NULL);
    if(e != NULL) {
        jsrBufferPrependstr(sb, "var _ = ");
        jsrBufferAppendstr(sb, "\nif _ != null then print(_) end");
        freeExpr(e);
    }
}

static void dorepl(bool ignoreEnv) {
    linenoiseSetCompletionCallback(completion);
    initImportPaths("./", ignoreEnv);
    printVersion();

    JStarBuffer src;
    jsrBufferInit(vm, &src);

    char* line;
    while((line = linenoise("J*>> ")) != NULL) {
        linenoiseHistoryAdd(line);
        int depth = countBlocks(line);
        jsrBufferAppendstr(&src, line);
        free(line);

        while(depth > 0 && (line = linenoise(".... ")) != NULL) {
            linenoiseHistoryAdd(line);
            jsrBufferAppendChar(&src, '\n');
            depth += countBlocks(line);
            jsrBufferAppendstr(&src, line);
            free(line);
        }

        addPrintIfExpr(&src);
        jsrEvaluate(vm, "<stdin>", src.data);
        jsrBufferClear(&src);
    }

    jsrBufferFree(&src);
    linenoiseHistoryFree();
}

// ---- Script execution ----

static void exitFree(int code) {
    jsrFreeVM(vm);
    exit(code);
}

static JStarResult execScript(const char* script, int argsCount, const char** args,
                              bool ignoreEnv) {
    jsrInitCommandLineArgs(vm, argsCount, args);

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

    JStarResult res = jsrEvaluate(vm, script, src);
    free(src);
    return res;
}

// ---- Main function and option parsing ----

typedef struct CLIOpts {
    const char* script;
    bool showVersion;
    bool interactive;
    bool ignoreEnv;
    const char* execStmt;
    const char** args;
    int argsCount;
} CLIOpts;

CLIOpts parseArguments(int argc, const char** argv) {
    CLIOpts opts = {0};

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Options"),
        OPT_BOOLEAN('v', "version", &opts.showVersion, "Print version information and exit", NULL,
                    0, 0),
        OPT_STRING('e', "exec", &opts.execStmt,
                   "Execute the given statement. If 'script' is provided it is executed after this",
                   NULL, 0, 0),
        OPT_BOOLEAN('i', "interactive", &opts.interactive,
                    "Enter the REPL after executing 'script' and/or '-e' statement", NULL, 0, 0),
        OPT_BOOLEAN('E', "ignore-env", &opts.ignoreEnv,
                    "Ignore environment variables such as JSTARPATH", NULL, 0, 0),
        OPT_END(),
    };

    static const char* const usage[] = {
        "jstar [options] [script [arguments]]",
        NULL,
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, ARGPARSE_STOP_AT_NON_OPTION);
    argparse_describe(&argparse, "J* a Lightweight Scripting Language", NULL);
    int nonOptsCount = argparse_parse(&argparse, argc, argv);

    if(nonOptsCount > 0) {
        opts.script = argv[0];
    }

    if(nonOptsCount > 1) {
        opts.args = &argv[1];
        opts.argsCount = nonOptsCount - 1;
    }

    return opts;
}

int main(int argc, const char** argv) {
    JStarConf conf;
    jsrInitConf(&conf);
    vm = jsrNewVM(&conf);

    CLIOpts opts = parseArguments(argc, argv);

    if(opts.showVersion) {
        printVersion();
        exitFree(EXIT_SUCCESS);
    }

    if(opts.execStmt) {
        JStarResult res = jsrEvaluate(vm, "<string>", opts.execStmt);
        if(opts.script && res == JSR_EVAL_SUCCESS) {
            res = execScript(opts.script, opts.argsCount, opts.args, opts.ignoreEnv);
        }
        if(!opts.interactive) exitFree(res);
    }

    if(opts.script && !opts.execStmt) {
        JStarResult res = execScript(opts.script, opts.argsCount, opts.args, opts.ignoreEnv);
        if(!opts.interactive) exitFree(res);
    }

    dorepl(opts.ignoreEnv);
    exitFree(EXIT_SUCCESS);
}
