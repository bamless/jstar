# J*

<p align="center"><img src="./docs/assets/images/JStar512.png"></p>

A dynamic lightweight scripting language

# How to use
## Repl
If the J* binary is executed without paramenters it will run the interpreter in *repl* mode. Here you can just type a line of code, press enter, and J* will execute it. You can also write multiline code, it will look like this:
```lua
J*>> for var i = 0; i < 3; i += 1 do
....   print("Hello World!")
.... end
Hello World!
Hello World!
Hello World!
J*>> _
```
When you eventually get bored, simply press Ctrl+d or Ctrl+c to exit the interpreter.

## Scripts
If you want to run a script, just pass its path as the first argument to J*. If you pass more than one argument, all the others will be forwarded to J* as *script arguments*.
You can then read them from the script this way:
```lua
import sys

if #sys.args > 0 then
  print(sys.args[0])
else
  raise Exception("No args provided")
end
```

For other examples of the language you can look at the **.bl** files in *src/vm/builtin*.

# How to build
The project uses **CMake** for building. Tested compilers include:
* GCC
* Clang
* MSVC

## Linux
Install **CMake**, navigate to the root of the project and issue:
```
mkdir build && cd build && cmake ../src && make -j
```
The J* executable will be created in the **build/bin** folder.

## Windows

#### Using MSVC

Install CMake for windows form [here](https://cmake.org/download/). Create a **build** folder in the root of the project. Open the CMake GUI and select **J*/src** as the input folder and **J*/build** as the output one. Click on **configure**. Once that is done, uncheck the **USE_COMPUTED_GOTOS** option (MSVC doesn't support computed gotos) and then click on **generate**. Project files for visual studio will be created in **build**. Then, simply import the project in visual studio and build the **J*** target.

#### Using MinGW-w64

If you want to use **MinGW** for building under windows the process is similar to the one described above for a linux build. I actually reccomend using **Msys2** for building using MinGW: \
First, you need to install Msys2 from [here](http://www.msys2.org/). Once installed launch the msys2 bash and run `pacman -Syu` to update the pakage databse. If needed, close MSYS2, run it again from Start menu. Update the rest with: `pacman -Su`. Once all is updated, run `pacman -S mingw-w64-x86_64-gcc` for 64 bit, or `pacman -S mingw-w64-i686-gcc` for 32 bit, to install Mingw-w64. Run `pacman -S base-devel` to install the tools needed for the build process (such as cmake, etc...). Once all of that is done, you can build the project exactly like you would on Linux. \
If you want to use CMD or Powershell instead of the Msys2 bash, you'll need to add \*MinGW inst. path\*/bin to your PATH.

## MacOS
I don't own a Mac, but since the project is standard c11, building on MacOS with Clang should be identical to building on Linux.

## Development

The project provides 2 build types: **Release** and **Debug**. The default build type is Release, it uses -O3 optimization level and strips all the debug symbols. If you want to debug/develop J* use the Debug build type (cmake -DCMAKE_BUILD_TYPE=debug) that includes debug information and doesn't apply any optimization.

## Compilation options

The project provides some cmake options to enable and disable certain functionalities:

| Variable value   | Description   |
| :--------------: | :------------ |
| NAN_TAGGING        | use the NaN tagging technique for storing the VM internal type (decreases the memory occupation of the VM)
| USE_COMPUTED_GOTOS | use computed gotos to implement the VM eval loop (branch predictor friendly, increases performance). Not all compilers support computed gotos, so if you're using one of them disable this option.
| DBG_PRINT_EXEC     | trace the execution of instructions of the virtual machine |
| DBG_STRESS_GC      | stress the garbage collector by calling it on every allocation |
| DBG_PRINT_GC       | trace the execution of the garbage collector |

The NAN_TAGGING and USE_COMPUTED_GOTOS options are set to ON as default.
