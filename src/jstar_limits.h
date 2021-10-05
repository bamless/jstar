#ifndef JSTAR_LIMITS_H
#define JSTAR_LIMITS_H

// Max number of frames, i.e. max J* call recursion depth.
// This limit is arbitrary, and it's only used for preventing infinite recursion.
// The only thing really limiting J* recursion depth is heap size, so you can increase this value
// as you like as long as you have the memory for it.
#define MAX_FRAMES 100000

// Maximum permitted number of reentrant calls.
// This limits only applies when doing reentrant calls inside the VM (for example: a native function
// calling a J* one, calling a native calling a J* one, and so on...) and it's enforced in order
// to prevent possible c stack overflows, as reentrant calls are implemented as c recurive calls.
// Decrease this value in case you get core dumps when dealing with deep reentrant call hierarchies.
// Conversely, you can increase this value if you need extra reentrant call depth and your platform
// has enough stack space for it.
#define MAX_REENTRANT 1000

// Maximum number of nested try-excepts allowed.
// Increasing this value will enable nesting more try-excepts, the memory consuption will be
// increased for each stack frame, leading to an overall increase in memory usage by the VM.
#define MAX_HANDLERS 6

#endif
