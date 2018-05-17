#include "object.h"

#include <stdio.h>

const char *typeName[] = {
	"OBJ_STRING", "OBJ_NATIVE", "OBJ_FUNCTION", "OBJ_CLASS", "OBJ_INST",
	"OBJ_LIST", "OBJ_MODULE", "OBJ_BOUND_METHOD"
};

void printObj(Obj *o) {
	switch(o->type) {
	case OBJ_STRING:
		printf("%s", ((ObjString*)o)->data);
		break;
	case OBJ_FUNCTION: {
		ObjFunction *f = (ObjFunction*) o;
		if(f->name != NULL) {
			printf("<func %s:%d>", f->name->data, f->argsCount);
		} else {
			printf("<func %d>", f->argsCount);
		}
		break;
	}
	case OBJ_NATIVE: {
		ObjNative *n = (ObjNative*) o;
		if(n->name != NULL) {
			printf("<native %s:%d>", n->name->data, n->argsCount);
		} else {
			printf("<native %d>", n->argsCount);
		}
		break;
	}
	case OBJ_CLASS: {
		ObjClass *cls = (ObjClass*) o;
		printf("<class %s:%s>", cls->name->data,
			cls->superCls == NULL ? "" : cls->superCls->name->data);
		break;
	}
	case OBJ_INST: {
		ObjInstance *i = (ObjInstance*) o;
		printf("<instance %s>", i->base.cls->name->data);
		break;
	}
	case OBJ_MODULE: {
		ObjModule *m = (ObjModule*) o;
		printf("<module %s>", m->name->data);
		break;
	}
	case OBJ_LIST: {
		ObjList *l = (ObjList*) o;
		printf("[");
		for(size_t i = 0; i < l->count; i++) {
			printValue(l->arr[i]);
			if(i != l->count - 1) printf(", ");
		}
		printf("]");
		break;
	}
	case OBJ_BOUND_METHOD: {
		ObjBoundMethod *b = (ObjBoundMethod*) o;
		printf("<bound method %p:%s>", (void*) b->bound, b->method->name->data);
		break;
	}
	}
}
