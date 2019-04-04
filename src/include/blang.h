#ifndef BLANG_H
#define BLANG_H

typedef enum {
	VM_EVAL_SUCCSESS, // The VM successfully executed the code
	VM_SYNTAX_ERR,    // A syntax error has been encountered in parsing
	VM_COMPILE_ERR,   // An error has been encountered during compilation
	VM_RUNTIME_ERR,   // An unhandled exception has reached the top of the stack
} EvalResult;

typedef struct BlangVM BlangVM;

BlangVM *blNewVM();
void blFreeVM(BlangVM *vm);

EvalResult blEvaluate(BlangVM *vm, const char *fpath, const char *src);
EvalResult blEvaluateModule(BlangVM *vm, const char *fpath, const char *name, const char *src);

void blInitCommandLineArgs(int argc, const char **argv);
void blAddImportPath(BlangVM *vm, const char *path);

typedef bool (*Native)(BlangVM *vm);

#define NATIVE(name) bool name(BlangVM *vm)

#endif