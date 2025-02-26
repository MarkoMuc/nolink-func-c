## [Part 2](https://blog.cloudflare.com/how-to-execute-an-object-file-part-2)

The two functions used in the previous example are completely self contained, meaning their output was computed solely based on the input, without any external code. Additional steps are needed to handle code with some dependencies.

### Handling relocations

If we use the `add5` functions inside the `add10` function, to calculate add 10, the result is not correct. 

Its time to analyze the `obj.o` file with `objdump --disassemble --section=.text obj.o`. The two important lines are the `e8 00 00 00 00` lines. Both are a `callq` assembly instructions, which is a near, relative call.

This variant of the call instruction consist of 5 bytes: the `0xe8` prefix and a 4-byte argument. The argument is the distance between the function we want to call and the current position(actually the next instruction).

The position of the two calls are `0x1f and 0x2c`. In the first case the argument is calculated as `0x0 - 0x24 = -0x24`. Since negatives are presented by their two's complement, the representation should be `0xffffffdc` and for the second call it would be `0xffffffcf`.

But the compiler does not calculate the correct argument. The compiler just leaves them at `0x00000000`. Let's try if our hypothesis is correct, by patching our loaded .text copy before executing it.

### Relocations

The problem with our toy object file is that both functions are declared with external linkage-the default setting for all functions and globals in C. The compiler is not sure where the add5 code will end up in the target binary. As such the compiler avoids making any assumptions and doesn't calculate the relative offset argument of the call insturctions. Let's try this by declaring the add5 fuction as `static`.

Now that the function is declared with internal linkeage, the compiler calculates the argument. Note that x86 is little endian. Also note that, since we can still call the static function inside the loader, the `static` keyword should not be used as a security feature to hide APIs from potential malicious users.

With external linkage, the compiler doesn't calculate the argument and leaving the calculation to the linker. But the linker needs some kind of clues to calculate it, this clues are called **relocations**. The relocatinos can be inspected with `readlef` by using `readelf --sections obj.o`.

We can find that the compiler created a `.rela.text` section, by convention, all relative sections have a `.rela` appendix.

To examine the relocations we use `readelf --relocs obj.o`. There are two relocation sections `.rela.eh_frame` and `.real.text`. The `.rela.eh_frame` section can be ignored this time.

Let's analyse the `.rela.text` section:

- *Offset* column tells us exactly where in the target section(.text in this case) the fix/adjustment is needed.
- *Info* is a combined value: the upper 32 bits - only 16 bits are shown in the output above - represent the index of the symbol in the symbol table, with respect to which the relocation si preformed. In our example it is 8 and if we run `readelf --symbols` we will see that it points to an entry corresponding to the add5 function. The lower 32 bits(4 in our case) is a relocation type
- *Type* describes the relocation type. This is a pseudo-column: readelf actually generates it from the lower 32bits of the *Info* field. Different relocation types have different formulas we need to apply to perform the relocation.
- *Sym. Value* may mean different things based on the relocation type, but most of the time it is the symbol offset with respect to which we perform the relocation.
- *Addend* is a constant we may need to use in the relocation formula. Depending on the relocation type, readelf actually adds the decoded symbol name to the output, so the column name is `Sym. Name + Addend` above but the actual field sotres the addend only.

In a nutshell. these entries tell us what we need to patch the `.text` section at offsets `0x20` and `0x2d`. To calculate it we apply the formula for `R_X86_64_PLT32` relocation. Based on [this](https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.95.pdf) ELF specification, the result of our relocation is word32. The formula we use is L + A - P, where:

- L is the address of the symbol, with respect to which the relocation is performed(add5 in our case).
- A is the constant addend(4 in our case).
- P is the address/offset, where we store the result of the relocation.

When the relocation formula references some symbol addresses or offsets, we shoudl use the actual - runtime in our case - addresses in the calculations. For example we use `text_runtime_base + 0x2d` as P for the second relocation and not just 0x2d.

Now we just implement this in the loader.

Our relocation also involved `text_runtime_base` address, which is not available at compile time. That's why the compiler could not calculate the call arguments in the first place and had to emit the relocations instead.
