#include "jstar.h"

#include "jsrparse/lex.h"
#include "jsrparse/parser.h"

#include "linenoise.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static JStarVM *vm;

static void header() {
    printf("J* Version %s\n", JSTAR_VERSION_STRING);
    printf("%s on %s\n", JSTAR_COMPILER, JSTAR_PLATFORM);
}

// Little hack to enable adding a tab in linenoise
static void completion(const char *buf, linenoiseCompletions *lc) {
    char indented[1025];
    snprintf(indented, sizeof(indented), "%s    ", buf);
    linenoiseAddCompletion(lc, indented);
}

static int countBlocks(const char *line) {
    Lexer lex;
    Token tok;

    initLexer(&lex, line);
    nextToken(&lex, &tok);

    int depth = 0;
    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
        switch(tok.type) {
        case TOK_BEGIN:
        case TOK_DO:
        case TOK_CLASS:
        case TOK_FUN:
        case TOK_TRY:
        case TOK_THEN:
            depth++;
            break;
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
        // print `_` if not null
        jsrBufferAppendstr(sb, "if _ != null then print(_) end");
    }
}

static void dorepl() {
    header();
    linenoiseSetCompletionCallback(completion);

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

static char *readSrcFile(const char *path) {
    FILE *srcFile = fopen(path, "rb");
    if(srcFile == NULL || errno == EISDIR) {
        if(srcFile) fclose(srcFile);
        perror("Error while reading input file");
        return NULL;
    }

    fseek(srcFile, 0, SEEK_END);
    size_t size = ftell(srcFile);
    rewind(srcFile);

    char *src = malloc(size + 1);
    if(src == NULL) {
        fclose(srcFile);
        fprintf(stderr, "Error while reading the file: out of memory.\n");
        return NULL;
    }

    size_t read = fread(src, sizeof(char), size, srcFile);
    if(read < size) {
        free(src);
        fclose(srcFile);
        fprintf(stderr, "Error: couldn't read file.\n");
        return NULL;
    }

    fclose(srcFile);
    src[read] = '\0';
    return src;
}

int main(int argc, const char **argv) {
    vm = jsrNewVM();

    EvalResult res = VM_EVAL_SUCCESS;
    if(argc == 1) {
        dorepl();
    } else {
        // set command line args for use in scripts
        jsrInitCommandLineArgs(vm, argc - 2, argv + 2);

        // set base import path to script's directory
        char *directory = strrchr(argv[1], '/');
        if(directory != NULL) {
            size_t length = directory - argv[1] + 1;
            char *path = calloc(length + 1, 1);
            memcpy(path, argv[1], length);
            jsrAddImportPath(vm, path);
            free(path);
        }

        // read file and evaluate
        char *src = readSrcFile(argv[1]);
        if(src == NULL) {
            exit(EXIT_FAILURE);
        }

        res = jsrEvaluate(vm, argv[1], src);
        free(src);
    }

    jsrFreeVM(vm);
    return res;
}
