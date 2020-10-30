---
layout: default
title: Home
nav_order: 1
description: "J*: A Lightweight Embeddable Scripting Language"
permalink: /
last_modified_date: 2020-04-27T17:54:08+0000
---

## The site is a work in progress.

---

# J*: A Lightweight Embeddable Scripting Language

**J\*** is a dynamic embeddable scripting language designed to be as easy as possible to embed into
another program. It arises from the need of having a modern scripting language with built-in
support for OOP whilst mantaning simplicity of use and a low memory footprint. It can be viewed as 
a middle ground between Python, a more complete scripting language with lots of features and 
libraries, and LUA, a small and compact language that is simple to embed but doesn't  provide OOP 
functionalities out of the box.  
J* tries to take the best of both worlds, implementing a fully featured class system while 
maintaining a small standard library and employing the use of a stack based API for communication 
among the language and host program, rendering embedding simple.

**J\*** is:
 - **Small**. The implementation spans only a handful of files and the memory footprint is low
   thanks to a minimal standard library that provides only essential functionalities.
 - **Easy to use**. The API is contained in a single header file and employs a stack based approach
   similar to the one of LUA, freeing the user from the burden of keeping track of memory owned by
   the language.
 - **Fully object oriented**. Every entity, from numbers to class instances, is an object in **J\***
 - **Modular**. A fully fledged module system makes it easy to split your code across multiple files
 - **Easily extensible**. The language can be easily extended by creating C functions callable from
   **J\*** using the API, or by importing [C extensions](https://github.com/bamless/jsocket) 
   provided as dynamic libraries.

To get a feel of the language, [try it in your browser](https://bamless.github.io/jstar/demo)!

# The **jstar** Command Line Interface

Besides the language implementation, a simple command line interface called `jstar` is provided to start using
the language without embedding it into another program.  
If the `jstar` binary is executed without
arguments it behaves like your usual read-eval-print loop, accepting a line at a time and executing
it:
```jstar
J*>> var helloWorld = 'Hello, World!'
J*>> print(helloWorld)
Hello, World!
J*>> _
```
You can even write multiline code, it will look like this:
```jstar
J*>> for var i = 0; i < 3; i += 1 do
....   print('Hello, World!')
.... end
Hello, World!
Hello, World!
Hello, World!
J*>> _
```
When you eventually get bored, simply press Ctrl+d or Ctrl+c to exit the interpreter.

If you instead want to execute code written in some file, you can pass it as an argument to `jstar`
and it will be executed. Passing more than one argument causes all but the first to be forwarded to
the language as **script arguments**. You can then read them from the script this way:
```jstar
if #argv > 0 then
  print('First argument: ', argv[0])
else
  raise Exception('No args provided')
end
```
The `jstar` executable can also accept various options that modify the behaviour of the command line
app. To see all of them alongside a description, simply pass the `-h` option to the executable.

In addition to being a useful tool to directly use the programming language, the command line interface
is also a good starting point to learn how **J\*** can be embedded in a program, as it uses the API
to implement all of its functionalities. You can find the code in [**cli/cli.c**](https://github.com/bamless/jstar/blob/master/cli/cli.c).

# Binaries

Precompiled binaries are provided for Windows and Linux for every major release. You can find them
[here](https://github.com/bamless/jstar/releases).

# Compilation

The **J\*** library requires a C99 compiler and CMake (>= 3.9) to be built, and is known to compile 
on OSX (Apple clang), Windows (both MSVC and MinGW-w64) and Linux (GCC, clang).

To build the provided **command line interface** `jstar`, a C++11 compiler is required as one of its
dependencies, linenoise-ng, is written in C++.

Additionally, if one wishes to modify the standard library (**.jsr** files in src/std),
a python interpreter (version >= 2.7) is required to generate header files from the code (CMake will
automatically take care of this).

You can clone the latest **J\*** sources using git:

```
git clone https://github.com/bamless/jstar.git
```

After cloning, use CMake to generate build files for your build system of choice and build the `all`
target to generate the language dynamic/static libraries and the command line interface. On 
UNIX-like systems this can be simply achieved by issuing this in the command line:

```
cd jstar; mkdir build; cd build; cmake ..; make -j
```

Once the build process is complete, you can install **J\*** by typing:

```
sudo make install
```

Various CMake options are available to switch on or off certain functionalities:

|    Option name       | Default | Description |
| :------------------: | :-----: | :---------- |
| JSTAR_NAN_TAGGING    |   ON    | Use the NaN tagging technique for storing the VM internal type. Decrases the memory footprint of the interpreter and increases speed |
| JSTAR_COMPUTED_GOTOS |   ON    | Use computed gotos to implement the VM eval loop. Branch predictor friendly, increases performance. Not all compilers support computed gotos (MSVC for example), so if you're using one of them disable this option |
|   JSTAR_INSTALL      |   ON    | Generate install targets for the chosen build system. Turn this off if including J* from another CMake project |
|       JSTAR_SYS      |   ON    | Include the 'sys' module in the language |
|       JSTAR_IO       |   ON    | Include the 'io' module in the language |
|      JSTAR_MATH      |   ON    | Include the 'math' module in the language |
|      JSTAR_DEBUG     |   ON    | Include the 'debug' module in the language |
|       JSTAR_RE       |   ON    | Include the 're' module in the language |
| JSTAR_DBG_PRINT_EXEC |   OFF   | Trace the execution of instructions of the virtual machine |
| JSTAR_DBG_STRESS_GC  |   OFF   | Stress the garbage collector by calling it on every allocation |
| JSTAR_DBG_PRINT_GC   |   OFF   | Trace the execution of the garbage collector |