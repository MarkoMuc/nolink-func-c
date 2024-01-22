/*
 * Most, if not all, of this code belongs to Ignat Korchagin
 * and is copied from his blog post https://blog.cloudflare.com/how-to-execute-an-object-file-part-(1..4)
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

//#define MONKEY_PATCH

/* from https://elixir.bootlin.com/linux/v5.11.6/source/arch/x86/include/asm/elf.h#L51 */
#define R_X86_64_PLT32 4

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

static uint8_t *section_runtime_base(const Elf64_Shdr *section){
    const char *section_name = shstrtab + section->sh_name;
    size_t section_name_len = strlen(section_name);

    // We only mmap .text section so far
    if(strlen(".text") == section_name_len && !strcmp(".text", section_name)    ){
        return text_runtime_base;
    }

    fprintf(stderr, "No runtime base address for section%s\n.", section_name);
    exit(ENOENT);
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

static void do_text_relocations(){
    // Here we use a hack : the name .rela.text is a convention, but not a rule. 
    // To actually figure out which section should be patched by these relocations we would need to examine the _rela_text_hdr.
    const Elf64_Shdr *rela_text_hdr = lookup_section(".rela.text");
    if (!rela_text_hdr) {
        fputs("Failed to find .rela.text\n", stderr);
        exit(ENOEXEC);
    }

    int num_relocations = rela_text_hdr->sh_size/rela_text_hdr->sh_entsize;
    const Elf64_Rela *relocations = (Elf64_Rela *)(obj.base + rela_text_hdr->sh_offset);
    
    for(int i = 0; i < num_relocations; i++){
        int symbol_idx = ELF64_R_SYM(relocations[i].r_info);
        int type = ELF64_R_TYPE(relocations[i].r_info);

        // Where to patch .text
        uint8_t *patch_offset = text_runtime_base + relocations[i].r_offset;
        uint8_t *symbol_address = section_runtime_base(&sections[symbols[symbol_idx].st_shndx]) + symbols[symbol_idx].st_value;
        
        switch(type)
        {
            case R_X86_64_PLT32:
                /* L + A - P, 32 bit output */
                *((uint32_t *)patch_offset) = symbol_address + relocations[i].r_addend - patch_offset;
                printf("Calculated relocation: 0x%08x\n", *((uint32_t *)patch_offset));
                break;
        
        }
    }
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
    
    // Copy the contents of .text section from the ELf file
    memcpy(text_runtime_base, obj.base + text_hdr->sh_offset, text_hdr->sh_size);

#ifdef MONKEY_PATCH
    // The first add5 callq ARGUMENT! is located at offset 0x20 and should be 0xffffffdc.
    // 0x1f is the instruction offset + 1 byte instruction prefix.
    *((uint32_t *)(text_runtime_base + 0x1f + 1)) = 0xffffffdc;
    
    // The second add5 call ARGUMENT! is located at offsset 0x2d and should be 0xffffffcf
    
    *((uint32_t *)(text_runtime_base + 0x2c + 1)) = 0xffffffcf;
#else
    do_text_relocations();
#endif


    // Make the .text copy readonly and executable
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
