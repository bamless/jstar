#include <stdio.h>
#include <string.h>

#include "vm.h"

int main() {
	VM vm;
	initVM(&vm);
	EvalResult res = evaluate(&vm, "var y = 3;\ndef func(x, y) {y = 4;} func(4, 5);");
	switch(res) {
	case VM_SYNTAX_ERR:
		fprintf(stderr, "Syntax error\n");
		break;
	case VM_COMPILE_ERR:
		fprintf(stderr, "Compile error\n");
		break;
	case VM_RUNTIME_ERR:
		fprintf(stderr, "Runtime error\n");
		break;
	case VM_EVAL_SUCCSESS: break;
	}
	freeVM(&vm);
}
