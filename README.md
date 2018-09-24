# blang
A dynamic lightweight scripting language

# How to use
## Repl
If the blang binary is executed without paramenters it will run the interpreter in *repl* mode. Here you can just type a line of code, press enter, and blang will execute it. You can also write multiline code using curly brackets, it will look like this:
```c#
blang>> for(var i in range(0, 3)) {
.......   print("Hello World!");
....... }
Hello World!
Hello World!
Hello World!
blang>> _
```
When you eventually get bored, simply press Ctrl+d or Ctrl+x to exit the interpreter.

## Scripts
If you want to run a script, just pass its path as the first argument to blang. If you pass more than one argument, all the other will be forwarded to blang as *script arguments*.
You can read them from the script like this:
```c#
import sys;

if(sys.args.size() > 0) {
  print(sys.args[0]);
} else {
  print("No args provided");
}
```

For other examples of the language you can look at the **.bl** files in *src/builtin*.

# How to build

## Linux
For building under linux just open a terminal in the poject's root and issue the `make` command. Make sure to have **make** and **gcc** installed. If you want to use another compiler symply modify the **CC** variable inside the Makefile

The following dependencies are required:
* **libreadline**
* **libjemalloc**

These libraries are often found in the distributions' repositories, if they're not you will need to build them manually.
Optionally, one can disable the libjemalloc dependency by setting the environment variable **USE_GLIBC_ALLOC** to 1 during the build:

`make USE_GLIBC_ALLOC=1`


## Windows

This project uses Msys2 and MinGW-w64 for building using **gcc** under Windows. First, you need to install msys2 from [here](http://www.msys2.org/). Once installed launch the msys2 bash and run `pacman -Syu` to update the pakage databse. If needed, close MSYS2, run it again from Start menu. Update the rest with: `pacman -Su`. Once all is updated, run `pacman -S mingw-w64-x86_64-gcc` for 64 bit, or `pacman -S mingw-w64-i686-gcc` for 32 bit, to install Mingw-w64. Run `pacman -S base-devel` to install the tools needed for the build process (such as make, etc...). Once all of that is done, you can build the project exactly like you would on Linux. Open the msys2 terminal, navigate to the project root, and run `make`.

The following dependencies are required for building under windows:
* **libreadline**

You can install libreadline in msys2 with the `pacman` command, like we did before for the base-devel package.

## Compilation options

During the compilation with make various environment variables can be used to enable or disable certain functionalities:

| Variable value   | Description   |
| :--------------: | :------------ |
| DBG_PRINT_EXEC=1 | trace the execution of instructions of the virtual machine |
| DBG_STRESS_GC=1  | stress the garbage collector by calling it on every allocation |
| DBG_PRINT_GC=1   | trace the execution of the garbage collector |

These environment variables can be exported in the terminal before executing `make`, or they can be set in a one liner like this:

`make OPTION1**=1 OPTION2=1 ...`
