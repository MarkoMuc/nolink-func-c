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

Using the calculation for `R_x86_64_32` type does not work. This time let's look at the disassembly of the object file
specifically lets look at the `.text` section

```
$objdump --disassembly --section=.text obj.o
0000000000000033 <get_hello>:
  33:   55                      push   %rbp
  34:   48 89 e5                mov    %rsp,%rbp
  37:   b8 00 00 00 00          mov    $0x0,%eax
  3c:   5d                      pop    %rbp
  3d:   c3                      ret
```

Instruction on line `0x37` corresponds with the relocation with offset `0x38`. This is a 32-bit move instruction
`MOV r32, imm32`. Since we are using a `mmap` the allocated memory for our three sections is addressed by a 64-bit
value. But the `mov` instruction expects at most a 32 bit value, and unlike the last time, this is not an offset
but an immediate value/absolute address. This means that our runtime address of the `.rodata` section does not fit
into this instruction. We will need another way to solve this relocation.

Solutions

1. The easiest solution is to tell `gcc` to generate position-independent code, instead of absolute addressing.
This can be done with `-fpic` and does not require chaning anything in the `loader`, since the relocation type
becomes `R_x86_64_PC32`.

```BASH
$gcc -fpic obj/obj.c
```

2. The second option also consists of using `gcc` options, this time we use `-mcmodel=large`. Checking the
[gnu gcc manual](https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html#index-mcmodel_003dlarge-4) we can find
the following description for this option

```
-mcmodel=large
    Generate code for the large model. This model makes no assumptions about addresses and sizes of sections.
```

The resulting code uses 64-bit absolute values when addressing, following is the new relocations table

```
Relocation section '.rela.text' at offset 0x320 contains 5 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000021  000600000001 R_X86_64_64       0000000000000000 add5 + 0
000000000035  000600000001 R_X86_64_64       0000000000000000 add5 + 0
000000000047  000400000001 R_X86_64_64       0000000000000000 .rodata + 0
000000000057  000300000001 R_X86_64_64       0000000000000000 .data + 0
00000000006c  000300000001 R_X86_64_64       0000000000000000 .data + 0
```

Now all relocations use a single new Type `R_X86_64_64`. Checking the ELF specification, this type requires
calculating relocations as `S + A`, which means that we just have to extend the `switch` case in `do_text_relocations`
to handle this type. Another key point is to use `uint64_t`, since we are patching a 64-bit value.

```C
  ...
  case R_X86_64_64:    // S + A
      *((uint64_t *)patch_offset) = (uint64_t)symbol_address + relocations[i].r_addend;
      break;
  ...
```

3. Since our issue is that the memory allocated by `mmap` is addressed by a 64-bit value, which doesn't fit into the
32-bit `mov` instruction that the compiler expected, we could solve this issue if we could force `mmap` to allocated
in a region addressable by 32-bit values. Checking the 
[options for mmap](https://man7.org/linux/man-pages/man2/mmap.2.html), we can see that there is a `MAP_32BIT` flag
that does just that.

Now all we have to do is change the line, where we call `mmap` to allocate space for the three sections.

```C
  ...
    text_runtime_base = mmap(NULL, full_section_size, PROT_READ | PROT_WRITE, 
                             MAP_PRIVATE
                             |MAP_ANONYMOUS
#ifdef MMAP_32
                             | MAP_32BIT
#endif
                             , -1, 0);
  ...
```

And then extend the `switch` case in `do_text_relocations` to handle `R_x86_64_32`. We also use `uintptr_t` to
suppress the warning which pops up due to casting to a smaller type.

```C
  ...
  case R_X86_64_32:    // S + A
      *((uint32_t *)patch_offset) = (uint32_t)(uintptr_t)(symbol_address + relocations[i].r_addend);
      break;
  ...
```


Note that the `MAP_32BIT` flag is added only if a macro exists, so to compile this code use the following command

```BASH
$gcc -DMMAP_32 -o bin/loader src/loader.c
```

**Note** that in this case, the loaded object code functions correctly, but we also now limit the maximum size of our
object code to the first 2 Gigabytes of the process address space.

4. There does exist a way to relocate correctly, without changing how we compile the code (or in case we only have
the object code). The logic behind is, to load the 64-bit address with a 64-bit immediate value compatible `move`
instruction, but this instruction takes up 10 bytes, while the current move only takes up 5 bytes.
Just overwriting the smaller `move` with the larger one can cause additional complication, since we can overwrite
the following instructions, we can change the pre calculated offsets that other instructions depend on, enlarge the
object code, which is not expected by the ELF file, etc.

What we need is a 5 byte or less instruction that will overwrite the `move` instruction and that can help us load
the address into memory. One such instruction is a `jump` with a 32-bit offset. We will use the jump, to safely
(without changing additional register as a `call` might do) move to a section of memory, where we can add our custom
instruction to load the 64-bit address.

The new section of memory will contain two instructions a 10 byte `move` and a 5 byte `jump`. The `move` will load
the 64-bit instructions, the `jump` will return back to the original object code. Now all we need to do is implement it.

Let's create a `Trampoline` structure, the name comes from **trampoline functions**.

```C
typedef struct {
    uint8_t data[15];
    uint8_t *startaddr;
} Trampoline;

static Trampoline *trampoline_runtime_base;
```

Since we are using relative jumps and the addresses need to be 32-bit, we should allocate memory for trampoline structure
at the same time as the other three sections. The amount of memory we need to allocate depends on the amount of
relocations where the object code expects a 32-bit value, but we only have a 64-bit value. Since we would need to know
the runtime addresses of the three sections, to calculate the relocation value, we will use a conservative estimate, this
means we allocate enough `Trampoline` structures to use for all relocations of type `R_x86_64_32`, this just means we
traverse the `.rela.text` sections and count the number of relocations that fit the type.

Afterwards we can allocate additional memory with the other three sections, so the offsets are more guaranteed to be
32 bit.

```C
    ...
    count_absolute_relocations();

    size_t full_section_size =
        page_align(text_hdr->sh_size) +
        page_align(data_hdr->sh_size) +
        page_align(rodata_hdr->sh_size) +
        page_align(sizeof(Trampoline) * num_absolute_relocs);
    ...

    trampoline_runtime_base = (Trampoline *) (rodata_runtime_base + page_align(rodata_hdr->sh_size));
    ...
```

We also need to mark the memory as executable and readable, as we did with the `.text` section.

```C
    ...
    if(mprotect(trampoline_runtime_base, page_align(sizeof(Trampoline) * num_absolute_relocs), PROT_READ | PROT_EXEC)) {
        perror("Failed to make \".text\" executable.");
        exit(errno);
    }
    ...
```

Afterwards we need to add another `case` statement to the `switch` in `do_text_relocations` function.

```C
static void do_text_relocations(void) {
  ...
    case R_X86_64_32:    // S + A
        const uint64_t reloc_address = (uint64_t)(symbol_address + relocations[i].r_addend);
        if((uintptr_t)reloc_address >> 32 > 0) {
            static size_t trampoline_idx = 0;

            const uint8_t INSTRUCTION_SIZE = 5;
            const uint8_t TRAMPOLINE_SIZE = sizeof(trampoline_runtime_base[0].data);
            trampoline_runtime_base[trampoline_idx].startaddr = &(trampoline_runtime_base[trampoline_idx].data[0]);

            uint8_t *instr_start_address = patch_offset - 1;
            const uint64_t reloc_address = (uint64_t)(symbol_address + relocations[i].r_addend);
            const uint8_t *tramp_offset = (uint8_t *)(trampoline_runtime_base[trampoline_idx].startaddr - (instr_start_address + 5));
            const uint32_t return_offset = (uint32_t)((instr_start_address + 5) - (trampoline_runtime_base[trampoline_idx].startaddr + 15));

            *instr_start_address = 0xE9;
            *((uint32_t *)patch_offset) = (uint32_t)(uintptr_t)tramp_offset;

            create_trampoline_func(&trampoline_runtime_base[trampoline_idx], reloc_address, return_offset);

            trampoline_idx++;
        } else {
            *((uint32_t *)patch_offset) = (uint32_t)(reloc_adress);
        }
        break;
    ...
}
```

The relocations first calculates the relocation and checks if it can fit into 32 bits, if it can, it just
relocates normally, otherwise we need to perform the trampoline procedure.

To implement the jump we need to calculated two offsets, the `tramp_offset` is used to jump to the
trampoline procedure that will load the 64 bit address, the `return_offset` is used to jump back
to the function code. The variable `patch_offset` carries the location of the instruction that needs
to be relocated, more specifically it points to the operand of the instruction. We **assume** that the
full instruction consists of 5 bytes, 4 bytes which are to contain the relocation address and 1 byte
for opcode. This means that the start of the instruction is `patch_offset - 1`. The start of the
instruction is needed, since this instruction will be overwritten with our `jmp` to the code
that will load the 64 bit address.

We overwrite the start of the instruction with `0xE9` which in x86 ISA means a `jmp` with
a 32 bit offset value. The offset to our trampoline procedure is calculated with

```C
  const uint8_t *tramp_offset = (uint8_t *)(trampoline_runtime_base[trampoline_idx].startaddr - (instr_start_address + 5));
```

The `instr_start_address +5` needs to be done, since the offset is relative to the next instruction
(the processor consumes the jump, which is 5 bytes, and then adds the offset to the current PC
counter or RIP in x86 ISA).

Then we need to calculate our `return_offset`, the only thing of note is to remember we need to jump
to the next instruction, since the current one is a `jump` and we will otherwise just jump back and forth
from the function to the trampoline function.

Now lets look at what the `create_trampoline_func` does

```C
static void create_trampoline_func(Trampoline *tramp, uint64_t address, uint32_t offset) {
    tramp->data[0] = 0x48; // RES.W
    tramp->data[1] = 0xB8; // MOV
    *((uint64_t*)&tramp->data[2]) = address; // 64-bit address

    tramp->data[10] = 0xE9; // JMP
    *((uint32_t*)&tramp->data[11]) = offset; // 32-bit offset
}
```

This function just creates a 64 bit move instruction followed by the 64 bit address, then
creates a jump back to the main function.

Now all relocations are successful, and we can see `Hello, World!`.

```
add5(42) = 47
add10(42) = 52
get_hello() = Hello, world!
get_var() = 5
set_var(42)
get_var() = 42
```

**Addendum**: The trampoline function expects to overwrite a 32 bit `mov` which takes a
register and immediate value. This means that the opcode might differ depending on what
register the compiler chose. The hardcoded value `0xB8` assumes that we are using the
`rex` register, we need to fix this and use the register the compiler chose.

This is a simple fix, just save the opcode of the instruction, before we overwrite it
with a `jmp` in `do_text_relocations`.

```C
static void do_text_relocations(void) {
  ...
    case R_X86_64_32:    // S + A
      ...
        const uint8_t mov_opcode = *instr_start_addr;
        *instr_start_address = 0xE9;
        *((uint32_t *)patch_offset) = (uint32_t)(uintptr_t)tramp_offset;

        create_trampoline_func(&trampoline_runtime_base[trampoline_idx], mov_opcode, reloc_address, return_offset);
      ...
  ...
}
```

And use the provided opcode in the trampoline function.

```C
static void create_trampoline_func(Trampoline *tramp, uint8_t mov_opcode,uint64_t address, uint32_t offset) {
    ...
    tramp->data[1] = mov_opcode; // MOV
    ...
}
```


