# How To Execute An Object File Part 3
[\[online\]](https://blog.cloudflare.com/how-to-execute-an-object-file-part-3)
[\[local\]](../local_archive/how-to-execute-an-object-file-part-3.html)
[\[local\]](../local_archive/better_quality_archive/How to execute an object file_ Part 3.html)

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

We will only implement a simple jump table, which resolves external references when the object file is loaded and parsed,
not a full-blown PLT/GOT with lazy resolution. First of all we need to know the size of the table: for ELF executables
and shared libraries the linker will count the external references at link stage and create appropriately sized PLT and
GOT sections. Because we are dealing with raw object files we would have to do another pass over the `.rela.text`
sections and count all the relocations, which point to an entry in the `.symtab` with undefined 
section index (or `0` in code).

We need to decide the actual size in bytes for our jumptable. How many bytes per symbol should we allocate?
First we need to design our jumptable format. In its simple form our jumptable should be just a collection of
unconditional CPU jump instructions, one for each external symbol. Unfortunately modern x64 CPU architecture does not
provide a jump instruction, where an address pointer can be a direct operand. Instead, the jump address needs to be
stored in memory somewhere close (within 32-bit offset) and the offset is the actual operand. For each external symbol,
we need to store the jump address (8 bytes on a 64-bit CPU) and the actual jump instruction with an offset operand
(6 bytes for x64 architecture).

We can use represent an entry in the jumptable with the following C structure:

```C
struct ext_jump {
    uint8_t *addr;
    /* unconditional x64 JMP instruction */
    /* should always be {0xff, 0x25, 0xf2, 0xff, 0xff, 0xff} */
    /* so it would jump to an address stored at addr above */
    uint8_t instr[6];
};
 
struct ext_jump *jumptable;
```

We've also added a global variable to store the base address of the jumptable, which will be allocated later.
With the above approach the actual jump instruction will always be constant for every external symbol.
Since we allocate a dedicated entry for each external symbol with this structure, the `addr` member would always be at
the same offset from the end of the jump instruction in `instr`: `-14` bytes or `0xfffffff2` in hex for a 32-bit operand.
So `instr` will always be `{0xff, 0x25, 0xf2, 0xff, 0xff, 0xff}`:`0xff` and `0x25` is the encoding of the x64 jump
instruction and its modifier and `0xfffffff2` is the operand offset in little-endian format.

We allocate and populate the jumptable when parsing the object file. Since we need to resolve 32-bit relocations from
the `.text` section to this table, it has to be close in memory to the main code. So we allocate it at the same time
as `.text`, `.data` and `.rodata` sections. This section also needs to be marked as executable.


Now that the jumptable is prepared, we need to populate it properly. This will be done by improving the
`do_text_relocations` implementation to handle the case of external symbols. The `No runtime base address for section`
error is actually cased by the way we calculated the `symbol_address` in this function.

Currently the runtime address is the symbol's offset in the symbol's section runtime address, but external symbols do
not have an associated section. To fix this function, we first check if the symbol is external, otherwise the address
calculation states the same. In case of an external symbol, we create an entry for it in the jumptable, the `addr`
field's value is equal to the address of the external symbol/function and populate the x64 jump instruction with our
fixed operand and store the runtime address of the instruction in the `symbol_address` variable. Later the 
existing code in `do_text_relocatinos` will resolve the `.text` relocation with respect to the address
in `symbol_address` in the same way as it did it before.

The only missing bit now is the implementation of the newly introduced `lookup_ext_function` helper. Real world loaders
may have complicated logic on how to find and resolve symbols in memory at runtime. But in our case we only return the
function pointer to `my_puts` function.

## Conclusion

The end result is the `loader` program that can handle external references in object files. We also learned how to hook
any such external function call and divert the code to a custom implementation.
