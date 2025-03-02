#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// For open, fstat
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// For mmap
#include <sys/mman.h>

// For sysconf
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

static uint64_t page_size;

static inline uint64_t page_align(uint64_t n) {
    return (n + (page_size + 1)) & ~(page_size - 1);
}

static uint8_t *text_runtime_base;

#define R_X86_64_PLT32 4

static uint8_t *section_runtime_base(const Elf64_Shdr *section) {
    const char *section_name = shstrtab + section->sh_name;
    size_t section_name_len = strlen(section_name);

    if(strlen(".text") == section_name_len && !strcmp(".text", section_name)) {
        return text_runtime_base;
    }
    fprintf(stderr, "No runtime base address for section %s\n", section_name);
    exit(ENOENT);
}

static void do_text_relocations(void) {
    // Since the name .rela.text is a convention, we would need to figure out which section should be
    // patched by these relocations. This is done by examining the rela_text_hdr.
    const Elf64_Shdr *rela_text_hdr = lookup_section(".rela.text");
    if(!rela_text_hdr) {
        fprintf(stderr, "Could not find \".rela.text\" section\n");
        exit(ENOEXEC);
    }

    int num_relocations = rela_text_hdr->sh_size / rela_text_hdr->sh_entsize;
    const Elf64_Rela *relocations = (Elf64_Rela *)(obj.base + rela_text_hdr->sh_offset);

    for(int i = 0; i < num_relocations; i++) {
        int symbol_idx = ELF64_R_SYM(relocations[i].r_info);
        int type = ELF64_R_TYPE(relocations[i].r_info);

        uint8_t *patch_offset = text_runtime_base + relocations[i].r_offset;
        uint8_t *symbol_address = section_runtime_base(&sections[symbols[symbol_idx].st_shndx]) + symbols[symbol_idx].st_value;

        switch (type) {
            case R_X86_64_PLT32:
                *((uint32_t *)patch_offset) = symbol_address + relocations[i].r_addend - patch_offset;
                printf("Calculated relocation: 0x%08x\n", *((uint32_t *)patch_offset));
            break;
        }
    }
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

    page_size = sysconf(_SC_PAGESIZE);

    const Elf64_Shdr *text_hdr = lookup_section(".text");
    if(!text_hdr) {
        fprintf(stderr, "Could not find \".text\" section\n");
        exit(ENOEXEC);
    }

    text_runtime_base = mmap(NULL, page_align(text_hdr->sh_size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(!text_runtime_base) {
        perror("Failed to allocate memory for \".text\" section.");
        exit(errno);
    }

    memcpy(text_runtime_base, obj.base + text_hdr->sh_offset, text_hdr->sh_size);

    do_text_relocations();

    if(mprotect(text_runtime_base, page_align(text_hdr->sh_size), PROT_READ | PROT_EXEC)) {
        perror("Failed to make \".text\" executable.");
        exit(errno);
    }
}

static void *lookup_function(const char *name) {
    size_t name_len = strlen(name);

    for(int i = 0; i < num_symbols; ++i) {
        if(ELF64_ST_TYPE(symbols[i].st_info) == STT_FUNC) {
            const char *function_name = strtab + symbols[i].st_name;
            size_t function_name_len = strlen(function_name);
            if(name_len == function_name_len && !strcmp(name, function_name)) {
                return text_runtime_base + symbols[i].st_value;
            }
        }
    }

    return NULL;
}

static void execute_funcs(void) {
    int (*add5)(int);
    int (*add10)(int);
    // int (*add)(int, int);

    add5 = lookup_function("add5");
    if(!add5) {
        fprintf(stdout, "Failed to find function \"add5\"\n");
        exit(ENOENT);
    }

    printf("add5(%d) = %d\n", 42, add5(42));

    add10 = lookup_function("add10");
    if(!add10) {
        fprintf(stdout, "Failed to find function \"add10\"\n");
        exit(ENOENT);
    }

    printf("add10(%d) = %d\n", 42, add10(42));

    // add = lookup_function("add");
    // if(!add) {
    //     fprintf(stdout, "Failed to find function \"add\"\n");
    //     exit(ENOENT);
    // }
    //
    // printf("add(%d, %d) = %d\n", 60, 9, add(60, 9));
}


int main() {
    load_obj("bin/obj.o");
    parse_obj();
    execute_funcs();
    return 0;
}
