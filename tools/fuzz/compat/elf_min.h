/* SPDX-License-Identifier: GPL-3.0-only */
/* The <elf.h> subset cm_elf_check uses, for hosts without <elf.h> (macOS): layouts and values as
 * in the ELF-64 specification and glibc's <elf.h>. Linux builds use the system header. */
#ifndef ELF_MIN_H
#define ELF_MIN_H
#include <stdint.h>
#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define EI_CLASS 4
#define EI_DATA 5
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_AARCH64 183
#define ET_DYN 3
#define SHT_DYNSYM 11
#define SHT_DYNAMIC 6
#define SHN_UNDEF 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STV_DEFAULT 0
#define DT_NULL 0
#define DT_NEEDED 1
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_VISIBILITY(o) ((o) & 0x03)
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;
typedef struct {
    uint32_t st_name;
    unsigned char st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} Elf64_Sym;
typedef struct {
    int64_t d_tag;
    union {
        uint64_t d_val, d_ptr;
    } d_un;
} Elf64_Dyn;
#endif
