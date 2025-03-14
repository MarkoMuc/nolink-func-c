# Calling functions without linking in C

## Main resources

- [How to execute an object file part 1.](https://blog.cloudflare.com/how-to-execute-an-object-file-part-1)
- [How to execute an object file part 2.](https://blog.cloudflare.com/how-to-execute-an-object-file-part-2)
- [How to execute an object file part 3.](https://blog.cloudflare.com/how-to-execute-an-object-file-part-3)
- [How to execute an object file part 4.](https://blog.cloudflare.com/how-to-execute-an-object-file-part-4)

## Additional material

- [ELF file](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format)
- [elf.h](https://man7.org/linux/man-pages/man5/elf.5.html)
- [readelf](https://man7.org/linux/man-pages/man1/readelf.1.html)
- [page](https://en.wikipedia.org/wiki/Page_(computer_memory))
- [data alignment](https://en.wikipedia.org/wiki/Data_structure_alignment)
- [x86 ISA](https://www.felixcloutier.com/x86)
- [System V ABI](https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.95.pdf)
- [Dynamic Linking](https://refspecs.linuxfoundation.org/ELF/zSeries/lzsabi0_zSeries/x2251.html)
- [Dynamic Loader](https://man7.org/linux/man-pages/man8/ld.so.8.html)

## Contents

- `src` contains the C main code.
- `obj` contains the obj code and C code to generate it.
- `notes/` contains notes for each part of the series.
- `local_archive/` contains a local archive of the four blogs. This is done in case they get pulled down one day. I do not claim any ownership over them and are there just for archival purposes.

# Explanatory material

Each part corresponds to a part in the *How to execute object files* series.

## Notes

- [How to execute an object file part 1](./notes/part1.md)
- [How to execute an object file part 2](./notes/part2.md)
- [How to execute an object file part 3](./notes/part3.md)
