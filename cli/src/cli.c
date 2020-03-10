#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstar.h"
#include "jsrparse/ast.h"
#include "jsrparse/lex.h"
#include "jsrparse/parser.h"
#include "jsrparse/token.h"

#include "linenoise.h"

#define JSTARPATH "JSTARPATH"

static JStarVM *vm;

// Little hack to enable adding a tab in linenoise
static void completion(const char *buf, linenoiseCompletions *lc) {
    char indented[1025];
    snprintf(indented, sizeof(indented), "%s    ", buf);
    linenoiseAddCompletion(lc, indented);
}

static void initImportPaths(const char *path) {
    jsrAddImportPath(vm, path);

    const char *jstarPath = getenv(JSTARPATH);
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

static int countBlocks(const char *line) {
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

static void addPrintIfExpr(JStarBuffer *sb) {
    Expr *e;
    // If the line is a (correctly formed) expression
    if((e = parseExpression(NULL, sb->data)) != NULL) {
        freeExpr(e);
        // assign the result of the expression to `_`
        jsrBufferPrependstr(sb, "var _ = ");
        jsrBufferAppendChar(sb, '\n');
        jsrBufferAppendstr(sb, "if _ != null then print(_) end");
    }
}

static void dorepl() {
    linenoiseSetCompletionCallback(completion);
    
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);

    initImportPaths("./");

    JStarBuffer src;
    jsrBufferInit(vm, &src);

    char *line;
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

int main(int argc, const char **argv) {
    vm = jsrNewVM();

    EvalResult res = VM_EVAL_SUCCESS;
    if(argc == 1) {
        dorepl();
    } else {
        // set command line args for use in scripts
        jsrInitCommandLineArgs(vm, argc - 2, &argv[2]);

        // set base import path to script's directory
        char *directory = strrchr(argv[1], '/');
        if(directory != NULL) {
            size_t length = directory - argv[1] + 1;
            char *path = calloc(length + 1, sizeof(char));
            memcpy(path, argv[1], length);
            initImportPaths(path);
            free(path);
        } else {
            initImportPaths("./");
        }

        // read file and evaluate
        char *src = jsrReadFile(argv[1]);
        if(src == NULL) {
            fprintf(stderr, "Error reading input file ");
            perror(argv[1]);
            exit(EXIT_FAILURE);
        }

        res = jsrEvaluate(vm, argv[1], src);
        free(src);
    }

    jsrFreeVM(vm);
    return res;
}
