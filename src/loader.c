#include <stdlib.h>
#include <stdio.h>

// For open, fstat
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// For mmap
#include <sys/mman.h>

#include <unistd.h>

// For parsing ELF files
#include <elf.h>

#include <error.h>
#include <errno.h>

typedef union {
    const Elf64_Ehdr *hdr;
    const uint8_t *base;
} objhdr;

static objhdr obj;

static void load_obj(const char* file) {
    struct stat sb;

    int fd = open(file, O_RDONLY);
    if (fd <= 0 ) {
        perror("Failed to open object file.");
        fprintf(stderr, "File \"%s\"\n", file);
        exit(errno);
    }

    if(fstat(fd, &sb)) {
        perror("Failed to get object file info");
        fprintf(stderr, "File \"%s\"\n", file);
        exit(errno);
    }

    obj.base = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(obj.base == MAP_FAILED) {
        perror("Failed to map object file");
        fprintf(stderr, "File \"%s\"\n", file);
        exit(errno);
    }

    close(fd);
}

int main() {
    load_obj("bin/obj.o");
    return 0;
}
