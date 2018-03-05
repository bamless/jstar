#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "memory.h"
#include "hashtable.h"

int main() {
	VM *vm = malloc(sizeof(*vm));
	initVM(vm);

	char *str = ALLOC(&vm->mem, strlen("string 1") + 1);
	strcpy(str, "string 1");
	ObjString *s = newString(&vm->mem, str, strlen(str));

	push(vm, OBJ_VAL(s)); //this should be reached on next gc passes

	str = ALLOC(&vm->mem, strlen("string 2") + 1);
	strcpy(str, "string 2");
	s = newString(&vm->mem, str, strlen(str));

	str = ALLOC(&vm->mem, strlen("interned string") + 1);
	strcpy(str, "interned string");
	s = newString(&vm->mem, str, strlen(str));
	hashTablePut(&vm->strings, s, NULL_VAL);

	str = ALLOC(&vm->mem, strlen("testFunc") + 1);
	strcpy(str, "testFunc");
	s = newString(&vm->mem, str, strlen(str));

	disableGC(&vm->mem, true);

	ObjFunction *fn = newFunction(&vm->mem, 4);
	fn->name = s;

	push(vm, OBJ_VAL(fn));

	disableGC(&vm->mem, false);

	str = ALLOC(&vm->mem, strlen("string 3") + 1);
	strcpy(str, "string 3");
	s = newString(&vm->mem, str, strlen(str));

	freeVM(vm);
	free(vm);
}
