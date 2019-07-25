#include "blang.h"

#include "blparse/lex.h"
#include "blparse/parser.h"

#include "linenoise.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static BlangVM *vm;

static void header() {
    const char blang_ascii_art[] = {
        0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x20, 0x5f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x0a, 0x20, 0x7c, 0x20, 0x5f,
        0x20, 0x29, 0x20, 0x7c, 0x5f, 0x5f, 0x20, 0x5f, 0x20, 0x5f, 0x20, 0x5f, 0x20, 0x20,
        0x5f, 0x5f, 0x20, 0x5f, 0x20, 0x0a, 0x20, 0x7c, 0x20, 0x5f, 0x20, 0x5c, 0x20, 0x2f,
        0x20, 0x5f, 0x60, 0x20, 0x7c, 0x20, 0x27, 0x20, 0x5c, 0x2f, 0x20, 0x5f, 0x60, 0x20,
        0x7c, 0x0a, 0x20, 0x7c, 0x5f, 0x5f, 0x5f, 0x2f, 0x5f, 0x5c, 0x5f, 0x5f, 0x2c, 0x5f,
        0x7c, 0x5f, 0x7c, 0x7c, 0x5f, 0x5c, 0x5f, 0x5f, 0x2c, 0x20, 0x7c, 0x0a, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x7c, 0x5f, 0x5f, 0x5f, 0x2f, 0x20, 0x0a, 0};
    printf("%s", blang_ascii_art);
    printf("Version %d.%d.%d\n", BLANG_VERSION_MAJOR, BLANG_VERSION_MINOR, BLANG_VERSION_PATCH);
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

static void addPrintIfExpr(BlBuffer *sb) {
    Parser p;

    Expr *e;
    char *src = sb->data;
    // If the line is a (correctly formed) expression
    if((e = parseExpression(&p, "", src, true)) != NULL) {
        freeExpr(e);
        // assign the result of the expression to `_`
        blBufferPrependstr(sb, "var _ = ");
        blBufferAppendChar(sb, '\n');
        // print `_` if not null
        blBufferAppendstr(sb, "if _ != null then print(_) end");
    }
}

static void dorepl() {
    header();
    linenoiseSetCompletionCallback(completion);

    BlBuffer src;
    blBufferInit(vm, &src);

    char *line;
    while((line = linenoise("blang>> ")) != NULL) {
        linenoiseHistoryAdd(line);

        int depth = countBlocks(line);
        blBufferAppendstr(&src, line);

        free(line);

        while(depth > 0 && (line = linenoise("....... ")) != NULL) {
            linenoiseHistoryAdd(line);

            blBufferAppendChar(&src, '\n');

            depth += countBlocks(line);
            blBufferAppendstr(&src, line);

            free(line);
        }

        addPrintIfExpr(&src);

        blEvaluate(vm, "<stdin>", src.data);
        blBufferClear(&src);
    }

    blBufferFree(&src);
    linenoiseHistoryFree();
}

static char *readSrcFile(const char *path) {
    FILE *srcFile = fopen(path, "rb+");
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
    vm = blNewVM();

    EvalResult res = VM_EVAL_SUCCSESS;
    if(argc == 1) {
        dorepl();
    } else {
        // set command line args for use in scripts
        blInitCommandLineArgs(argc - 2, argv + 2);

        // set base import path to script's directory
        char *directory = strrchr(argv[1], '/');
        if(directory != NULL) {
            size_t length = directory - argv[1] + 1;
            char *path = calloc(length + 1, 1);
            memcpy(path, argv[1], length);

            blAddImportPath(vm, path);

            free(path);
        }

        // read file and evaluate
        char *src = readSrcFile(argv[1]);
        if(src == NULL) {
            exit(EXIT_FAILURE);
        }

        res = blEvaluate(vm, argv[1], src);
        free(src);
    }

    blFreeVM(vm);
    return res;
}
