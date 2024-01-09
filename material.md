# Explanatory material

Each part corresponds to a part in the *How to execute object files* series.

## Part 1

Compilation is a multi stage process. First the source code is compiled into machine code, which is saved on object files, afterwards the linker assembbles all object files into a singular working program and binding referneces together. The output of a linker is an executable file.

Today executables are also dynamically linked, which means all code is not present during compile time, instead the code is "borrowed" during runtime from shared libraries.

Runtime linking is done by the OS, which invokes the dynamic loader. The dynamic loader finds the shared libraries and maps them to the process' address space and resolves all dependencies our code has on them.

### Loading an object file into the process memory

First create a simple `obj.c` file with simple functions and compile into an object file ``gcc -c obj.c``.

Executing the object file is the same as using it as a library. Since we do not use dynamic linking, the dynamic loader will not load the object file for us, we must do this manually. The object file needs to be loaded into the RAM.

