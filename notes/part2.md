# How To Execute An Object File Part 2
[\[online\]](https://blog.cloudflare.com/how-to-execute-an-object-file-part-2)
[\[local\]](../local_archive/how-to-execute-an-object-file-part-2.html)
[\[local\]](../local_archive/better_quality_archive/how-to-execute-an-object-file-part-2.html)

The two functions used in the previous example are completely self-contained, meaning their output was computed solely based on the input, without any external code or data dependencies. Additional steps are needed to handle code with dependencies.

## Handling relocations

Our function `add10` can be implemented by applying `add5` two times. Our new `.o` file will be as follows

```C
  ...
  int add10(int num) {
    num = add5(num);
    return add5(num);
  }
```

If we use the `add5` functions inside the `add10` function, to calculate add 10, the result is not correct. 

Its time to analyze the `obj.o` file this is done with:
```BASH
$ objdump --disassemble --section=.text obj.o
```

The two important lines in the output are the `e8 00 00 00 00` lines. Both are a `callq` assembly instructions, which represents a near, relative, displacement relative call.

This variant of the call instruction consist of 5 bytes: the `0xe8` prefix and a 4-byte argument. The argument is the distance between the function we want to call and the current position (more accurately the next instruction after `callq`).

The position of the two calls are `0x1f` and `0x2c`. In the first case the argument is calculated as `0x0 - 0x24 = -0x24`, where `0x0` is the starting location of `add5`. A negative number means we need to jump backwards. Since negatives are presented by their two's complement, the representation should be `0xffffffdc` and for the second call it would be `0xffffffcf`.

But the compiler does not calculate the correct argument. The compiler just leaves them at `0x00000000`. Let's try if our hypothesis is correct, by patching our loaded `.text` copy before executing it.

## Relocations

The problem with our toy object file is that both functions are declared with external linkage-the default setting for all functions and globals in C. The compiler is not sure where the `add5` code will end up in the target binary. As such the compiler avoids making any assumptions and doesn't calculate the relative offset argument of the call instructions. Let's try this by declaring the `add5` function as `static`.

Now that the function is declared with internal linkage, the compiler calculates the argument. Note that x86 is little endian.

Even thought the function `add5` is declared as static, we can still call it from the `loader` tool, basically ignoring the fact that it is an "internal" function now. Because of this, the `static` keyword should not be used as a security feature to hide APIs from potential malicious users.

With external linkage, the compiler doesn't calculate the argument and leaving the calculation to the linker. But the linker needs some kind of clues to calculate it, this clues or instructions for the later stages, are called **relocations**. The relocations can be inspected with `readlef` by using:

```BASH
$ readelf --sections obj.o
```

We can find that the compiler created a `.rela.text` section, by convention, all relative sections have a `.rela` appendix.

To examine the relocations we use:

```BASH
$ readelf --relocs obj.o
```

There are two relocation sections `.rela.eh_frame` and `.real.text`. The `.rela.eh_frame` section can be ignored this time.

Let's further analyse the `.rela.text` section:

- *Offset* column tells us exactly where in the target section (`.text` in this case) the fix/adjustment is needed.
- *Info* is a combined value: the upper 32 bits - only 16 bits are shown in the output above - represent the index of the symbol in the symbol table, with respect to which the relocation is preformed. In our example it is 8 and if we run `readelf --symbols` we will see that it points to an entry corresponding to the `add5` function. The lower 32 bits (4 in our case) is a relocation type
- *Type* describes the relocation type. This is a pseudo-column: `readelf` actually generates it from the lower 32bits of the *Info* field. Different relocation types have different formulas we need to apply to perform the relocation.
- *Sym. Value* may mean different things based on the relocation type, but most of the time it is the symbol offset with respect to which we perform the relocation.
- *Addend* is a constant we may need to use in the relocation formula. Depending on the relocation type, `readelf` actually adds the decoded symbol name to the output, so the column name is `Sym. Name + Addend` above but the actual field stores the addend only.

In a nutshell, these entries tell us what we need to patch the `.text` section at offsets `0x20` and `0x2d`. To calculate it we apply the formula for `R_X86_64_PLT32` relocation. Based on the ELF specification, the result of our relocation is `word32`. The formula we use is `L + A - P`, where:

- L is the address of the symbol, with respect to which the relocation is performed (`add5` in our case).
- A is the constant addend (`4` in our case).
- P is the address/offset, where we store the result of the relocation.

When the relocation formula references some symbol addresses or offsets, we should use the actual - runtime in our case -addresses in the calculations. For example we use `text_runtime_base + 0x2d` as P for the second relocation and not just `0x2d`.

Now we just implement this in the loader.

Our relocation also involved `text_runtime_base` address, which is not available at compile time. That's why the compiler could not calculate the call arguments in the first place and had to emit the relocations instead.

## Handling constant data and global variables

So far the object files contained only executable code with no state. That is, the imported functions could compute their output solely based on the inputs. Lets add some constants and global variables to our obj file.

```C
  ...

  const char *get_hello(void) {
      return "Hello, world!";
  }

  static int var = 5;

  int get_var(void) {
      return var;
  }

  void set_var(int num) {
      var = num;
  }
```

Running the `loader` crashes because it does not find the `.rodata` section. This section has been added to hold the `Hello, world!` string.

Lets check what new sections, symbols and relocations are in out new object file

```BASH
$ readelf --sections obj.o
$ readelf --relocs obj.o
$ readelf --symbols obj.o
```

We can see that an additional section `.rodata` has been added, section `.data` has also been populated (previously it had size 0). Symbol table section also now contains `var` and `get/set_var`. To import our newly added code and make it work, we also need to map `.data` and `.rodata` sections in addition to the `.text` section and process three `R_x86_64_PC32` relocations that were added.

There is one caveat. The ELF specification defines that `R_x86_64_PC32` relocation produces a 32-bit output similar to the `R_X86_64_PLT32` relocation. This means that the distance in memory between the patched position in `.text` and the referenced symbol has to be small enough to fit into a 32-bit value (note that this is a signed 32-bit integer). We use `mmap` to allocate memory for the object section copies, but `mmap` may allocate the mapping anywhere in the process address. This means that the separately allocated sections may end up having `.rodata` or `.data` section mapped too far away from the `.text` section and will not be able to process the `R_x86_64_PC32` relocations.

This can be solved by allocating enough memory with one `mmap` call to store all the needed sections.

We also need to implement relocation for Type `R_X86_64_PLT32`. The relocation formula is `S + A - P`. In our case S is essentially the same as L for `R_x86_64_PC32`, we can just reuse our previous implementation.

## Extra

If we only follow the article, our `loader` does not work correctly. The `loader` fails to correctly relocate the symbol
for the constant string `Hello, world!` found in the `.rodata` section. The following is the output of our `loader`
indicating there is an issue.

```
...
get_hello() = (null)
...
```

If we once again take a look at the relocations, we can see that one of the relocation types is actually `R_x86_64_32`.
The corresponding relocations line is as following:

```
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000038  00040000000a R_X86_64_32       0000000000000000 .rodata + 0
```

If we check the offset, we can make sure that this relates to the constant string used in `get_hello` function.
The issue arises when we are calculating the relocation offset. The formula for `R_x86_64_32` is actually `S + A`.
This means we need to add another `case` in our `switch` statement.
