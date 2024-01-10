/*
 * Most, if not all, of this code belongs to Ignat Korchagin
 * and is copied from his blog post https://blog.cloudflare.com/how-to-execute-an-object-file-part-1
 *
 *
*/

#include <asm-generic/errno-base.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <elf.h>
#include <errno.h>

typedef union {
    const Elf64_Ehdr *hdr;
    const uint8_t *base;
} objhdr;

static objhdr obj;

// Sections table
static const Elf64_Shdr *sections;
static const char *shstrtab = NULL;

// Symbols table
static const Elf64_Sym *symbols;
static int num_symbols;
static const char *strtab = NULL;

static uint64_t page_size;

static uint8_t *text_runtime_base;

static inline uint64_t page_align(uint64_t n){
    return (n + (page_size - 1)) & ~(page_size - 1);
}

static void load_obj(){
    struct stat sb;
    int fd = open("obj/obj.o", O_RDONLY);
    
    if (fd <= 0) {
        perror("Cannot open obj.o");
        exit(errno);
    }

    if(fstat(fd, &sb)) {
        perror("Failed to get obj.o's file info.");
        exit(errno);
    }
    obj.base = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(obj.base == MAP_FAILED){
        perror("Mapping obj.o failed.");
        exit(errno);
    }

    close(fd);

}

static const Elf64_Shdr *lookup_section(const char *name){
    size_t name_len = strlen(name);

    for (Elf64_Half i = 0; i < obj.hdr->e_shnum; i++) {
        const char *section_name = shstrtab + sections[i].sh_name;
        size_t section_name_len = strlen(section_name);
        if(name_len == section_name_len && !strcmp(name, section_name)){
            if(sections[i].sh_size){
                return sections + i;
            }
        }
    }

    return NULL;
}

static void parse_obj(){
    // Sections table offset
    sections = (const Elf64_Shdr *)(obj.base + obj.hdr->e_shoff);
    shstrtab = (const char *)(obj.base + sections[obj.hdr->e_shstrndx].sh_offset);

    const Elf64_Shdr *symtab_hdr = lookup_section(".symtab");
    if(!symtab_hdr){
        fputs("Failed to find .symtab\n", stderr);
        exit(ENOEXEC);
    }
    
    symbols = (const Elf64_Sym*)(obj.base + symtab_hdr->sh_offset);
    num_symbols = symtab_hdr->sh_size / symtab_hdr->sh_entsize;

    const Elf64_Shdr *strtab_hdr = lookup_section(".strtab");
    if(!strtab_hdr){
        fputs("Failed to find .strtab\n", stderr);
        exit(ENOEXEC);
    }

    strtab = (const char *) (obj.base + strtab_hdr->sh_offset);
    
    page_size = sysconf(_SC_PAGESIZE);
    
    const Elf64_Shdr *text_hdr = lookup_section(".text");
    if (!text_hdr) {
        fputs("Failed to find .text\n", stderr);
        exit(ENOEXEC);
    }

    text_runtime_base = mmap(NULL, page_align(text_hdr->sh_size), PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

    if (text_runtime_base == MAP_FAILED) {
        perror("Failed to allocate memory for .text");
        exit(errno);
    }

    memcpy(text_runtime_base, obj.base + text_hdr->sh_offset, text_hdr->sh_size);

    if (mprotect(text_runtime_base, page_align(text_hdr->sh_size), PROT_READ | PROT_EXEC)) {
        perror("Failed to make .text executable");
        exit(errno);
    }

}

static void *lookup_function(const char *name){
    size_t name_len = strlen(name);
    for (int i = 0; i < num_symbols; i++) {
        if (ELF64_ST_TYPE(symbols[i].st_info) == STT_FUNC) {
            const char *function_name = strtab + symbols[i].st_name;
            size_t function_name_len = strlen(function_name);

            if (name_len == function_name_len && !strcmp(name, function_name)) {
                return text_runtime_base + symbols[i].st_value;
            }
        }
    }
}

static void execute_funcs(){
    int (*add5)(int);
    int (*add10)(int);
    
    add5 = lookup_function("add5");
    if (!add5) {
        fputs("Failed to find add5 function\n", stderr);
        exit(ENOENT);
    }
    
    puts("Executing add5...");
    printf("add5(%d) = %d\n", 42, add5(42));
    
    add10 = lookup_function("add10");
    if (!add10) {
        fputs("Failed to find add10 function\n", stderr);
        exit(ENOENT);
    }
    puts("Executing add10...");
    printf("add10(%d) = %d\n", 42, add10(42));
}

int main(int argc, char *argv[]){
    load_obj();
    parse_obj();
    execute_funcs();
    return EXIT_SUCCESS;
}
