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


