#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef __unix
#include <unistd.h>
#elif _WIN32
#include <windows.h>
#endif

#include "vm.h"

static void interactiveEval(VM *vm) {
	for(;;) {
		char *src = readline("blang>> ");
		if(src == NULL) {
			printf("\n");
			break;
		}

		if(strlen(src) == 0) {
			free(src);
			continue;
		}

		add_history(src);
		evaluate(vm, src);
		free(src);
	}
}

static char* readSrcFile(const char *path) {
	FILE *srcFile = fopen(path, "r+");
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
	VM vm;
	initVM(&vm);

	EvalResult res = VM_EVAL_SUCCSESS;
	if(argc == 1) {
		interactiveEval(&vm);
	} else {
		initCommandLineArgs(argc - 2, argv + 2);

		char *src = readSrcFile(argv[1]);
		if(src == NULL) {
			return VM_GENERIC_ERR;
		}

		char *directory = strrchr(argv[1], '/');
		if(directory != NULL) {
			size_t length = directory - argv[1] + 1;
			char *pwd = malloc(length + 1);
			memcpy(pwd, argv[1], length);
			pwd[length] = '\0';

#ifdef __unix
			if(chdir(pwd))
#elif _WIN32
			if(!SetCurrentDirectory(pwd))
#endif
			{
				fprintf(stderr, "Failed to change cwd.\n");
				exit(1);
			}


			free(pwd);
		}

		res = evaluate(&vm, src);
		free(src);
	}

	freeVM(&vm);
	exit(res);
}
