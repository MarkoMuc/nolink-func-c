#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

// Sections table
static const Elf64_Shdr *sections;
static const char *shstrtab = NULL;

// Symbols table
static const Elf64_Sym *symbols;

// Number of entries in the symbols table
static int num_symbols;
static const char *strtab = NULL;

static const Elf64_Shdr *lookup_section(const char *name) {
    size_t name_len = strlen(name);
    for(Elf64_Half i = 0; i < obj.hdr->e_shnum; ++i) {
        const char *section_name = shstrtab + sections[i].sh_name;
        size_t section_name_len = strlen(section_name);
        if(name_len == section_name_len && !strcmp(name, section_name)) {
            if(sections[i].sh_size) {
                return &sections[i];
            }
        }
    }

    return NULL;
}

static void parse_obj(void) {
    sections = (const Elf64_Shdr *)(obj.base + obj.hdr->e_shoff);
    shstrtab = (const char*)(obj.base + sections[obj.hdr->e_shstrndx].sh_offset);

    const Elf64_Shdr *symtab_hdr = lookup_section(".symtab");
    if(!symtab_hdr) {
        fprintf(stderr, "Could not find \".symtab\" section\n");
        exit(ENOEXEC);
    }

    symbols = (const Elf64_Sym *)(obj.base + symtab_hdr->sh_offset);
    num_symbols = symtab_hdr->sh_size / symtab_hdr->sh_entsize;

    const Elf64_Shdr *strtab_hdr = lookup_section(".strtab");
    if(!strtab_hdr) {
        fprintf(stderr, "Could not find \".strtab\" section\n");
        exit(ENOEXEC);
    }

    strtab = (const char *)(obj.base + strtab_hdr->sh_offset);

}


int main() {
    load_obj("bin/obj.o");
    return 0;
}
