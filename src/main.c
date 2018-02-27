#include <stdio.h>

#include "parser.h"
#include "value.h"
#include "object.h"
#include "hashtable.h"

int main(int argc, char **argv) {
	if(argc < 2) {
		fprintf(stderr, "%s\n", "No source code privided.");
		exit(1);
	}

	Parser p;
	Program *program = parse(&p, argv[1]);

	if(p.hadError) {
		freeProgram(program);
		exit(2);
	}

	LinkedList *n;
	foreach(n, program->stmts) {
		Stmt *s = n->elem;
		printf("%d\n", s->type);

		if(s->type == FUNCDECL) {
			printf("%.*s\n", (int) s->funcDecl.id.length, s->funcDecl.id.name);
		}
	}

	freeProgram(program);
	
	HashTable t;
	initHashTable(&t);

	ObjString *k = malloc(sizeof(*k));
	k->length = 4;
	k->data = "ciao";
	k->hash = 1000;

	Value v = NUM_VAL(20);
	hashTablePut(&t, k, v);

	ObjString *k2 = malloc(sizeof(*k));
	k2->length = 5;
	k2->data = "ciao2";
	k2->hash = 1000;

	v = NUM_VAL(50);
	hashTablePut(&t, k2, v);

	printf("%s\n", 	hashTableGet(&t, k2, &v) ? "found!" : "nope");
	printf("%g\n", 	AS_NUM(v));

	printf("%s\n", hashTableDel(&t, k2) ? "deleted!" : "not deleted...");

	v = NUM_VAL(32434);
	hashTablePut(&t, k, v);

	printf("%s\n", 	hashTableGet(&t, k, &v) ? "found!" : "nope");
	printf("%g\n", AS_NUM(v));

	freeHashTable(&t);
	free(k);
	free(k2);
}
