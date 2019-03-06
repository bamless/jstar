#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "vm.h"
#include "options.h"

#include "linenoise/linenoise.h"
#include "util/stringbuf.h"

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

static int charCount(const char *str, char c) {
	int count = 0;
	size_t len = strlen(str);
	for(size_t i = 0; i < len; i++) {
		if(str[i] == c) {
			count++;
		}
	}
	return count;
}

static void interactiveEval(BlangVM *vm) {
	header();
	linenoiseSetCompletionCallback(completion);

	StringBuffer src;
	sbuf_create(&src);

	char *line;
	while((line = linenoise("blang>> ")) != NULL) {
		if(strlen(line) == 0) {
			free(line);
			continue;
		}

		if(strcmp(line, "\\clear") == 0) {
			linenoiseClearScreen();
			free(line);
			continue;
		}

		sbuf_appendstr(&src, line);
		linenoiseHistoryAdd(line);

		int depth = charCount(line, '{') - charCount(line, '}');

		free(line);

		if(depth > 0) {
			while((line = linenoise("....... ")) != NULL) {
				if(strlen(line) == 0) {
					free(line);
					continue;
				}

				sbuf_appendchar(&src, '\n');
				sbuf_appendstr(&src, line);
				linenoiseHistoryAdd(line);

				depth += charCount(line, '{') - charCount(line, '}');

				free(line);

				if(depth <= 0)
					break;
			}
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
	BlangVM *vm = blNewVM();

	EvalResult res = VM_EVAL_SUCCSESS;
	if(argc == 1)
	{
		interactiveEval(vm);
	}
	else
	{
		//set command line args for use in scripts
		blInitCommandLineArgs(argc - 2, argv + 2);

		//set base import path to script's directory
		char *directory = strrchr(argv[1], '/');
		if(directory != NULL) {
			size_t length = directory - argv[1] + 1;
			char *path = malloc(length + 1);
			memcpy(path, argv[1], length);
			path[length] = '\0';

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
