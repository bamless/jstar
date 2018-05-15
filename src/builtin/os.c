#include "os.h"

NATIVE(getOS) {
#ifdef __linux
	return OBJ_VAL(copyString(vm, "Linux", 5));
#elif _WIN32
	return OBJ_VAL(copyString(vm, "Win32", 5));
#elif __APPLE__
	return OBJ_VAL(copyString(vm, "OSX", 3));
#endif
}
