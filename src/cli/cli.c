#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "vm.h"
#include "options.h"

#include "parse/lex.h"

#include "linenoise/linenoise.h"
#include "util/stringbuf.h"

static Lexer lex;
static Token prev;
static Token cur;

static BlangVM *vm;

static Token *lexNext() {
	prev = cur;
	nextToken(&lex, &cur);
	return &cur;
}

static void header() {
	const char blang_ascii_art[] = {
	  0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x20, 0x5f, 0x20, 0x20, 0x20, 0x20, 0x20,
	  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x0a,
	  0x20, 0x7c, 0x20, 0x5f, 0x20, 0x29, 0x20, 0x7c, 0x5f, 0x5f, 0x20, 0x5f,
	  0x20, 0x5f, 0x20, 0x5f, 0x20, 0x20, 0x5f, 0x5f, 0x20, 0x5f, 0x20, 0x0a,
	  0x20, 0x7c, 0x20, 0x5f, 0x20, 0x5c, 0x20, 0x2f, 0x20, 0x5f, 0x60, 0x20,
	  0x7c, 0x20, 0x27, 0x20, 0x5c, 0x2f, 0x20, 0x5f, 0x60, 0x20, 0x7c, 0x0a,
	  0x20, 0x7c, 0x5f, 0x5f, 0x5f, 0x2f, 0x5f, 0x5c, 0x5f, 0x5f, 0x2c, 0x5f,
	  0x7c, 0x5f, 0x7c, 0x7c, 0x5f, 0x5c, 0x5f, 0x5f, 0x2c, 0x20, 0x7c, 0x0a,
	  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	  0x20, 0x20, 0x20, 0x20, 0x20, 0x7c, 0x5f, 0x5f, 0x5f, 0x2f, 0x20, 0x0a,
	  0
	};
	printf("%s", blang_ascii_art);
	printf("Version %d.%d.%d\n", BLANG_VERSION_MAJOR, BLANG_VERSION_MINOR, BLANG_VERSION_PATCH);
}

static void completion(const char *buf, linenoiseCompletions *lc) {
	char *ret = malloc(strlen(buf) + 5);
	strcpy(ret, buf);
	strcat(ret, "    ");
	linenoiseAddCompletion(lc, ret);
	free(ret);
}

static int countBlocks() {
	static bool inelif = false;

	int depth = 0;
	while(cur.type != TOK_EOF && cur.type != TOK_NEWLINE) {
		switch(cur.type) {
		case TOK_BEGIN:
		case TOK_DO:
		case TOK_CLASS:
		case TOK_DEF:
		case TOK_TRY:
			depth++;
			break;
		case TOK_THEN:
			if(inelif)
				inelif = false;
			else
				depth++;
			break;
		case TOK_END:
			depth--;
			break;
		case TOK_ELIF:
			inelif = true;
			break;
		default: break;
		}
		lexNext();
	}
	return depth;
}

static void dorepl() {
	header();
	linenoiseSetCompletionCallback(completion);

	blEvaluate(vm, "<stdin>", "def _(s) if s != null then print(s) end end");

	StringBuffer src;
	sbuf_create(&src);

	char *line;
	while((line = linenoise("blang>> ")) != NULL) {
		linenoiseHistoryAdd(line);

		initLexer(&lex, line);
		TokenType type = lexNext()->type;

		bool expr = type == TOK_NUMBER ||
		            type == TOK_TRUE ||
					type == TOK_FALSE ||
					type == TOK_IDENTIFIER ||
					type == TOK_LPAREN ||
					type == TOK_MINUS ||
					type == TOK_BANG ||
					type == TOK_STRING;

		if(expr) sbuf_appendstr(&src, "_(");
		sbuf_appendstr(&src, line);
		if(expr) sbuf_appendstr(&src, ")");

		sbuf_appendchar(&src, '\n');

		int depth = countBlocks();

		free(line);

		while(depth > 0 && (line = linenoise("....... ")) != NULL) {
			linenoiseHistoryAdd(line);

			initLexer(&lex, line);
			lexNext();

			sbuf_appendstr(&src, line);
			sbuf_appendchar(&src, '\n');

			depth += countBlocks();

			free(line);
		}

		blEvaluate(vm, "<stdin>", sbuf_get_backing_buf(&src));
		sbuf_clear(&src);
	}

	sbuf_destroy(&src);
}

static char* readSrcFile(const char *path) {
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
	if(argc == 1)
	{
		dorepl();
	}
	else
	{
		//set command line args for use in scripts
		blInitCommandLineArgs(argc - 2, argv + 2);

		//set base import path to script's directory
		char *directory = strrchr(argv[1], '/');
		if(directory != NULL) {
			size_t length = directory - argv[1] + 1;
			char *path = calloc(length + 1, 1);
			memcpy(path, argv[1], length);

			blAddImportPath(vm, path);

			free(path);
		}

		//read file and evaluate
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
