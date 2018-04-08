#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "vm.h"

int main() {
	VM vm;
	initVM(&vm);

	for(;;) {
		char *src = readline(">>> ");
		if(src == NULL) {
			printf("\n");
			break;
		}

		if(strlen(src) == 0) {
			free(src);
			continue;
		}

		add_history(src);

		EvalResult res = evaluate(&vm, src);
		switch(res) {
		case VM_SYNTAX_ERR:
			fprintf(stderr, "Syntax error.\n");
			break;
		case VM_COMPILE_ERR:
			fprintf(stderr, "Compile error.\n");
			break;
		case VM_RUNTIME_ERR:
			fprintf(stderr, "Runtime error.\n");
			break;
		case VM_EVAL_SUCCSESS: break;
		}

		free(src);
	}

	freeVM(&vm);
}
