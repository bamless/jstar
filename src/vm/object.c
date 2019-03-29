#include "object.h"

#include <stdio.h>

#ifdef DBG_PRINT_GC
DEFINE_TO_STRING(ObjType, OBJTYPE);
#endif

void printObj(Obj *o) {
	switch(o->type) {
	case OBJ_STRING:
		printf("%s", ((ObjString*)o)->data);
		break;
	case OBJ_FUNCTION: {
		ObjFunction *f = (ObjFunction*) o;
		if(f->c.name != NULL) {
			printf("<func %s:%d>", f->c.name->data, f->c.argsCount);
		} else {
			printf("<func %d>", f->c.argsCount);
		}
		break;
	}
	case OBJ_NATIVE: {
		ObjNative *n = (ObjNative*) o;
		if(n->c.name != NULL) {
			printf("<native %s:%d>", n->c.name->data, n->c.argsCount);
		} else {
			printf("<native %d>", n->c.argsCount);
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
	case OBJ_TUPLE: {
		ObjTuple *t = (ObjTuple*) o;
		printf("(");
		for(size_t i = 0; i < t->size; i++) {
			printValue(t->arr[i]);
			if(i != t->size - 1) printf(", ");
		}
		printf(")");
		break;
	}
	case OBJ_BOUND_METHOD: {
		ObjBoundMethod *b = (ObjBoundMethod*) o;
		char *name = b->method->type == OBJ_FUNCTION ?
		                ((ObjFunction*)b->method)->c.name->data :
		                ((ObjNative*)b->method)->c.name->data;
		printf("<bound method ");
		printValue(b->bound);
		printf(":%s>", name);
		break;
	}
	case OBJ_STACK_TRACE:
		printf("<stacktrace %p>", (void*) o);
		break;
	case OBJ_CLOSURE:
		printf("<closure %p>", (void*) o);
		break;
	case OBJ_UPVALUE:
		printf("<upvalue %p>", (void*) o);
		break;
	case OBJ_RANGE:
		printf("<range %p>", (void*) o);
		break;
	}
}
