#include <stdio.h>

#include "parser.h"
#include "value.h"

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
}
