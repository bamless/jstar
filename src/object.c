#include "object.h"

#include <stdio.h>

const char *typeName[] = {
	"OBJ_STRING", "OBJ_NATIVE", "OBJ_FUNCTION", "OBJ_CLASS", "OBJ_INST"
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
		printf("<instance %s>", i->cls->name->data);
	}
	}
}
