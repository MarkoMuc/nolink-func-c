# How To Execute An Object File Part 3
[\[online\]](https://blog.cloudflare.com/how-to-execute-an-object-file-part-3)
[\[local\]](../local_archive/how-to-execute-an-object-file-part-3.html)

This part covers external dependencies in the object file.

## Dealing with external libraries

Lets add an external library call to our object file:

```C
#include <stdio.h>
  ...
  void say_hello(void) {
    puts("Hello, world!");
  }
```

Running the loader results in the following error:

```
No runtime base address for section
```

If we check the relocations we find new entries, one of them is `puts - 4` with type `R_X86_64_PLT32` which we should be able to process. The problem seems to be elsewhere, lets follow the symbol index which is `0xc` or `12` in this case.

The symbol table entry number `12` is as follows

```
   Num:    Value          Size Type    Bind   Vis      Ndx Name
    ...
    11: 000000000000005d    17 FUNC    GLOBAL DEFAULT    1 say_hello
    12: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND puts
```

The section index for the `puts` function is `UND` which is essentially `0`. This is because the symbol `puts` is an external dependency, and it is not implemented in our `obj.o` file, therefore it can't be a part of any section within `obj.o`. We need so somehow point the code to jump to a `puts` implementation.

Our `loader` actually already has access to the C library `puts` function, since the `loader` uses `puts`. But technically it doesn't have to be the C library `puts`, just some puts implementation. Let's implement our own custom `puts` function in the `loader`:

```C
  ...
  static int my_puts(const char *s) {
      puts("my_puts executed");
      return puts(s);
  }
  ...
```

Now we have a `puts` implementation and its runtime address, we only need to write logic in the loader to resolve the relocation by instructing the code to jump to the correct function. There is one issue, we can only use 32-bit relative relocations. `R_X86_64_PLT32` is also a 32-bit relative relocation, so it has the same requirements. We cannot use the same trick as in the previous part, because `my_puts` is part of the `loader` itself and we do not have control over where in the address space the operating system places the `loader` code.

To do this, we can borrow the approach used in shared libraries.

## Exploring PLT/GOT

Often executables have dependencies on shared libraries and shared libraries have dependencies on other shared libraries. All of the different pieces of a complete runtime program may be mapped to random ranges in the process address space.

When a shared library or an ELF executable is linked, the linker enumerates all the external references and creates two or more additional sections in the ELF file. The two mandatory ones are the Procedure Linkage Table (PLT) and the Global Offset Table (GOT).

In a nutshell PLT/GOT is just a jumptable for external code. At the linking stage the linker resolves all external 32-bit relative relocations with respect to a locally generated PLT/GOT table. It can do that, because this table would become part of the final ELF file itself, so it will be close to the main code, when the file is mapped into memory at runtime. Later, at runtime the dynamic loader populates PLT/GOT tables for every loaded ELF file (both the executable and the shared libraries) with the runtime addresses of all the dependencies. Eventually, when the program code calls some external library function, the CPU jumps through the local PLT/GOT table to the final code.

Why do we need two ELF sections to implement one jumptable? This is because real world PLT/GOT is a bit more complex. Resolving all external references at runtime may significantly slow down program startup time, so symbol resolution is implemented via a "lazy approach". This means a reference is resolved by the dynamic loader only when the code actually tries to call a particular function. If the main application code never calls a library function, that reference will never be resolved.

## Implementing a simplified PLT/GOT

We will only implement a simple jump table, which resolves external references when the object file is loaded and parsed, not a full-blown PLT/GOT with lazy resolution. First of all we need to know the size of the table: for ELF executables and shared libraries the linker will count the external references at link stage and create appropriately sized PLT and GOT sections. Because we are dealing with raw object files we would have to do another pass over the `.rela.text` sections and count all the relocations, which point to an entry in the `.symtab` with undefined section index (or `0` in code).


