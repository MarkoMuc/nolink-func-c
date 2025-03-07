# How To Execute An Object File Part 1
[\[online\]](https://blog.cloudflare.com/how-to-execute-an-object-file-part-1)
[\[local\]](../local_archive/how-to-execute-an-object-file-part-1.html)
[\[local\]](../local_archive/better_quality_archive/how-to-execute-an-object-file-part-1.html)

Compilation is a multi-stage process. First, the source code is compiled into machine code, which is saved in object files, afterwards, the linker assembles all object files into a single working program and binds references together. The output of a linker is an executable file.

Today, executables are also dynamically linked, which means all code is not present during compile time, instead, the code is "borrowed" during runtime from shared libraries. This process is called *runtime linking*.

Runtime linking is done by the OS, which invokes the *dynamic loader*. The dynamic loader finds the shared libraries, maps them to the process's address space, and resolves all dependencies our code has on them.

## Loading an object file into the process memory

The following is the contents of the object file, that we will load into memory.

```C
int add5(int num){
    return num + 5;
}

int add10(int num){
    return num + 10;
}

int add(int a, int b) {
    return a + b;
}
```

The file can be found in `obj/obj_part1.c`. To create an object file, we compile it with `gcc -c obj_part1.c`.

Executing the object file is the same as using it as a library. Since we do not use dynamic linking, the dynamic loader will not load the object file for us, we must do this manually. The object file needs to be loaded into the RAM.

At this stage, the object file is copied into memory. This can be done by filling up a buffer, but since an object file can be large, we instead map it into memory when needed. To do this, `mmap` is used, so the OS lazily reads the part of the file we need when we need it. Note that this only maps in the object file, afterwards we need to parse the object file, which is actually an ELF file.

## Parsing ELF files

Segments (also known as program headers) and sections are the main parts of an ELF file.

Different sections contain different types of ELF data e.g.: 
- executable code
- constant data
- global variables

On the other hand, segments don't contain any data. They give describe to the OS, how to properly load sections into RAM for the executable to work. Segments do not contain sections, they just indicate to the OS where in memory a particular section should be loaded and where is the access pattern for this memory (read, write or execute).

Furthermore, object files do not contain any segments at all, since an object file is not meant to be directly loaded by the OS. ELF segments are generated by the linker. The following command can be used to read the ELF files:

```BASH
$ readelf --segments
$ readelf --sections
```

Short descriptions of various ELF sections:

- `.text`: contains the executable code. This section is the primary area of interest for us.
- `.data` and `.bss`: contains global and static local variables. The `.data` section contains variables with an initial value, `.bss` just reserves space for variables with no initial value.
- `.rodata`: contains constant data, mostly string and byte arrays. Can be missing if no strings are used.
- `.symtab`: information about the symbols in the object file (functions, global variables, constants, etc.), including external symbols if they are present.
- `.strtab` and `.shstrtab`: contains packed strings for the ELF file. These are strings describing the names of other ELF structures, like symbols from `.symtab` or section names. ELF format aims to make its structures compact and of a fixed size, so all strings are stored in one place, and their respective data structures just reference them as an offset in either `.strtab` or `.shstrtab` sections, instead of storing the full string locally.

## The `.symtab` section

Our functions are stored in the `.text` sections, but since the section is just a byte blob, we need to extract the locations of each function. To do this, we use the `.symtab` section. To read and visualize it use the following command:

```BASH
$ readelf --symbols
```

The output of `readelf` command has various fields:
  - `Ndx` column: tells us the index of the section, where the symbol is located.
  - `Type`: symbol type, in our case we care about  `FUNC` type.
  - `Size`: size of each symbol.
  - `Value`: the name is misleading, this represents the offset from the start of the containing section in this context. In our case functions are offset from the `.text` section.

## Finding and executing a function form an object file

The plan to execute a function:

1. Find ELF sections table and `.shstrtab` section (to lookup sections in the section table).
2. Find the `.symtab` and `.strtab` (to lookup symbols by name in the symbols table) sections.
3. Find the `.text` section and copy it into the RAM with executable permissions.
4. Find function offsets in the symbol table.
5. Execute the functions.

To find the `.shstrtab` section in the table, we need to perform a name lookup. `.shstrtab` is then used to lookup other sections by their name. Note that the section table does not contain the string name of the section. Instead the `sh_name` parameter is an offset in the `.shstrtab` section, which points to a string name. Using the name lookup, we can now find the `.symtab` and `.strtab` sections.

In the next part we need to copy the `.text` section into a different location in RAM and add execute permissions. This is done for multiple reasons:

- Many CPU architectures don't allow (or there's a performance penalty) at executing unaligned memory (4 kB for x86 systems). The `.text` section is not guaranteed to be positioned at a page aligned offset, because the on-disk version of the ELF file aims to be compact rather than convenient.
- We may need to modify some bytes in the `.text` section to perform relocations. For example, if we forget to use the `MAP_PRIVATE` flag, when mapping the ELF file, our modifications may propagate to the underlying file and corrupt it.
- Each section (that is needed during runtime) require different memory permission bits: the `.text` section needs to be both readable and executable, but not writable (being writable and executable would be a bad security practice). The `.data` and `.bss` sections need to be readable and writable to support global variables but not executable. The `.rodata` section should be readonly, because it holds constant data. To support this, each section must be allocated on a page boundary as we can only set memory permission bits on whole pages and not custom rages. Therefore, we need to create new, page aligned memory ranges for these sections and copy the data there.

Creating a page aligned copy for a section requires knowing the page size.

To execute the `.text` section we need to:

1. Find the `.text` section metadata in the sections table.
2. Allocate a chunk of memory to hold the `.text` section copy.
3. Actually copy the `.text` section to the newly allocated memory.
4. Make the `.text` section executable, so we can later call functions from it.

Now we can import code from an object file and execute. This example only covered self-contained functions, that do not reference any global variables or constants, and does not have any external dependencies.

Note that this `loader.c` omits a bounds checking and additional ELF integrity checks. 

## Extra

More detailed explanation on how to search/parse the ELF file.

- Searching for sections:
  1. Finding the `section header table`: from the ELF header we get the `e_shoff` value. This value represents an offset relative to the start of the ELF file, which use to locate the section table.
  2. Finding the `.shstrtab` section: from the ELF header we get the `e_shstrndx` value. This is an offset relative to the `section header table`. From the section table we get the `sh_offset` value, this is an offset relative to the ELF file, which we use to locate the `.shstrtab` section.
  3. Finding a specific section consists of traversing the `section table` and taking the `sh_name` value, which is an offset in the `.shstrtab` section, here we can find the section name. If the name matches the section we are looking for, we can use the `sh_offset` value to locate the section.
- Searching for symbols/functions:
  1. Finding the `.symtab` and `.strtab` section: find the two sections as specified above.
  2. Finding a function: traverse the `.symtab`, use the `st_name` value, which is an offset in the `.strtab` containing the function/symbol name. If the name matches, the `st_value` value in the `.symtab` represents an offset in the `.text` section where the function is located. Note that how `st_value` value should be interpreted depends on what type of symbol we are reading.
