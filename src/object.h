#include <stdbool.h>

typedef enum ObjType {
	OBJ_STRING, OBJ_NATIVE, OBJ_FUNCTION
} ObjType;

typedef struct Obj {
	ObjType type;
	bool marked;
	struct Obj *next;
} Obj;
