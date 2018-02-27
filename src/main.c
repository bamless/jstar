#include <stdio.h>

#include "parser.h"
#include "value.h"
#include "object.h"
#include "hashtable.h"
#include "chunk.h"

int main(int argc, char **argv) {
	// if(argc < 2) {
	// 	fprintf(stderr, "%s\n", "No source code privided.");
	// 	exit(1);
	// }
	//
	// Parser p;
	// Program *program = parse(&p, argv[1]);
	//
	// if(p.hadError) {
	// 	freeProgram(program);
	// 	exit(2);
	// }
	//
	// LinkedList *n;
	// foreach(n, program->stmts) {
	// 	Stmt *s = n->elem;
	// 	printf("%d\n", s->type);
	//
	// 	if(s->type == FUNCDECL) {
	// 		printf("%.*s\n", (int) s->funcDecl.id.length, s->funcDecl.id.name);
	// 	}
	// }
	//
	// freeProgram(program);

	Chunk c;
	initChunk(&c);

	writeByte(&c, 1, 1);
	writeByte(&c, 1, 1);
	writeByte(&c, 1, 1);
	writeByte(&c, 1, 1);
	writeByte(&c, 1, 2);
	writeByte(&c, 1, 2);
	writeByte(&c, 1, 3);
	writeByte(&c, 1, 3);
	writeByte(&c, 1, 3);
	writeByte(&c, 1, 3);
	writeByte(&c, 1, 3);
	writeByte(&c, 1, 3);
	writeByte(&c, 1, 4);
	writeByte(&c, 1, 4);
	writeByte(&c, 1, 4);

	for(size_t i = 0; i < c.linesCount; i++) {
		printf("%d\n", c.lines[i]);
	}

	puts("");

	for(size_t i = 0; i < c.count; i++) {
		printf("%d\n", getBytecodeSrcLine(&c, i));
	}

	freeChunk(&c);
}
