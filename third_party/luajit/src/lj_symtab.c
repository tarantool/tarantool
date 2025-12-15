/*
** Implementation of symbol table for profilers.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lj_symtab_c
#define LUA_CORE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lj_symtab.h"

#if LJ_HASRESOLVER

#include <linux/limits.h>
#include <elf.h>
#include <link.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <unistd.h>
#include "lj_gc.h"
#endif

static const unsigned char ljs_header[] = {'l', 'j', 's', LJS_CURRENT_VERSION,
                                          0x0, 0x0, 0x0};

#if LJ_HASJIT

void lj_symtab_dump_trace(struct lj_wbuf *out, const GCtrace *trace)
{
  GCproto *pt = &gcref(trace->startpt)->pt;
  BCLine lineno = 0;

  const BCIns *startpc = mref(trace->startpc, const BCIns);
  lj_assertX(startpc >= proto_bc(pt) && startpc < proto_bc(pt) + pt->sizebc,
	     "start trace PC out of range");

  lineno = lj_debug_line(pt, proto_bcpos(pt, startpc));

  lj_wbuf_addu64(out, (uint64_t)trace->traceno);
  /*
  ** The information about the prototype, associated with the
  ** trace's start has already been dumped, as it is anchored
  ** via the trace and is not collected while the trace is alive.
  ** For this reason, we do not need to repeat dumping the chunk
  ** name for the prototype.
  */
  lj_wbuf_addu64(out, (uintptr_t)pt);
  lj_wbuf_addu64(out, (uint64_t)lineno);
}

#endif /* LJ_HASJIT */

void lj_symtab_dump_proto(struct lj_wbuf *out, const GCproto *pt)
{
  lj_wbuf_addu64(out, (uintptr_t)pt);
  lj_wbuf_addstring(out, proto_chunknamestr(pt));
  lj_wbuf_addu64(out, (uint64_t)pt->firstline);
}

#if LJ_HASRESOLVER

struct ghashtab_header {
  uint32_t nbuckets;
  uint32_t symoffset;
  uint32_t bloom_size;
  uint32_t bloom_shift;
};

static uint32_t ghashtab_size(ElfW(Addr) ghashtab)
{
  /*
  ** There is no easy way to get count of symbols in GNU hashtable, so the
  ** only way to do this is to take highest possible non-empty bucket and
  ** iterate through its symbols until the last chain is over.
  */
  uint32_t last_entry = 0;

  const uint32_t *chain = NULL;
  struct ghashtab_header *header = (struct ghashtab_header *)ghashtab;
  /*
  ** sizeof(size_t) returns 8, if compiled with 64-bit compiler, and 4 if
  ** compiled with 32-bit compiler. It is the best option to determine which
  ** kind of CPU we are running on.
  */
  const char *buckets = (char *)ghashtab + sizeof(struct ghashtab_header) +
                        sizeof(size_t) * header->bloom_size;

  uint32_t *cur_bucket = (uint32_t *)buckets;
  uint32_t i;
  for (i = 0; i < header->nbuckets; ++i) {
    if (last_entry < *cur_bucket)
      last_entry = *cur_bucket;
    cur_bucket++;
  }

  if (last_entry < header->symoffset)
    return header->symoffset;

  chain = (uint32_t *)(buckets + sizeof(uint32_t) * header->nbuckets);
  /* The chain ends with the lowest bit set to 1. */
  while (!(chain[last_entry - header->symoffset] & 1))
    last_entry++;

  return ++last_entry;
}

static void write_c_symtab(ElfW(Sym *) sym, char *strtab, ElfW(Addr) so_addr,
			   size_t sym_cnt, const uint8_t header,
			   struct lj_wbuf *buf)
{
  /*
  ** Index 0 in ELF symtab is used to represent undefined symbols. Hence, we
  ** can just start with index 1.
  **
  ** For more information, see:
  ** https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-79797.html
  */

  ElfW(Word) sym_index;
  for (sym_index = 1; sym_index < sym_cnt; sym_index++) {
    /*
    ** ELF32_ST_TYPE and ELF64_ST_TYPE are the same, so we can use
    ** ELF32_ST_TYPE for both 64-bit and 32-bit ELFs.
    **
    ** For more, see https://github.com/torvalds/linux/blob/9137eda53752ef73148e42b0d7640a00f1bc96b1/include/uapi/linux/elf.h#L135
    */
    if (ELF32_ST_TYPE(sym[sym_index].st_info) == STT_FUNC &&
        sym[sym_index].st_name != 0) {
      char *sym_name = &strtab[sym[sym_index].st_name];
      lj_wbuf_addbyte(buf, header);
      lj_wbuf_addu64(buf, sym[sym_index].st_value + so_addr);
      lj_wbuf_addstring(buf, sym_name);
    }
  }
}

static int dump_sht_symtab(const char *elf_name, struct lj_wbuf *buf,
			   lua_State *L, const uint8_t header,
			   const ElfW(Addr) so_addr)
{
  int status = 0;

  char *strtab = NULL;
  ElfW(Shdr *) section_headers = NULL;
  ElfW(Sym *) sym = NULL;
  ElfW(Ehdr) elf_header = {};

  ElfW(Off) sym_off = 0;
  ElfW(Off) strtab_off = 0;

  size_t sym_cnt = 0;
  size_t strtab_size = 0;
  size_t header_index = 0;

  size_t shoff = 0; /* Section headers offset. */
  size_t shnum = 0; /* Section headers number. */
  size_t shentsize = 0; /* Section header entry size. */

  FILE *elf_file = fopen(elf_name, "rb");

  if (elf_file == NULL)
    return -1;

  if (fread(&elf_header, sizeof(elf_header), 1, elf_file) != sizeof(elf_header)
      && ferror(elf_file) != 0)
    goto error;
  if (memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0)
    /* Not a valid ELF file. */
    goto error;

  shoff = elf_header.e_shoff;
  shnum = elf_header.e_shnum;
  shentsize = elf_header.e_shentsize;

  if (shoff == 0 || shnum == 0 || shentsize == 0)
    /* No sections in ELF. */
    goto error;

  /*
  ** Memory occupied by section headers is unlikely to be more than 160B, but
  ** 32-bit and 64-bit ELF files may have sections of different sizes and some
  ** of the sections may duiplicate, so we need to take that into account.
  */
  section_headers = lj_mem_new(L, shnum * shentsize);
  if (section_headers == NULL)
    goto error;

  if (fseek(elf_file, shoff, SEEK_SET) != 0)
    goto error;

  if (fread(section_headers, shentsize, shnum, elf_file) != shentsize * shnum
      && ferror(elf_file) != 0)
    goto error;

  for (header_index = 0; header_index < shnum; ++header_index) {
    if (section_headers[header_index].sh_type == SHT_SYMTAB) {
      ElfW(Shdr) sym_hdr = section_headers[header_index];
      ElfW(Shdr) strtab_hdr = section_headers[sym_hdr.sh_link];
      size_t symtab_size = sym_hdr.sh_size;

      sym_off = sym_hdr.sh_offset;
      sym_cnt = symtab_size / sym_hdr.sh_entsize;

      strtab_off = strtab_hdr.sh_offset;
      strtab_size = strtab_hdr.sh_size;
      break;
    }
  }

  if (sym_off == 0 || strtab_off == 0 || sym_cnt == 0)
    goto error;

  /* Load symtab into memory. */
  sym = lj_mem_new(L, sym_cnt * sizeof(ElfW(Sym)));
  if (sym == NULL)
    goto error;
  if (fseek(elf_file, sym_off, SEEK_SET) != 0)
    goto error;
  if (fread(sym, sizeof(ElfW(Sym)), sym_cnt, elf_file) !=
      sizeof(ElfW(Sym)) * sym_cnt && ferror(elf_file) != 0)
    goto error;


  /* Load strtab into memory. */
  strtab = lj_mem_new(L, strtab_size * sizeof(char));
  if (strtab == NULL)
    goto error;
  if (fseek(elf_file, strtab_off, SEEK_SET) != 0)
    goto error;
  if (fread(strtab, sizeof(char), strtab_size, elf_file) !=
      sizeof(char) * strtab_size && ferror(elf_file) != 0)
    goto error;

  write_c_symtab(sym, strtab, so_addr, sym_cnt, header, buf);

  goto end;

error:
  status = -1;

end:
  if (sym != NULL)
    lj_mem_free(G(L), sym, sym_cnt * sizeof(ElfW(Sym)));
  if(strtab != NULL)
    lj_mem_free(G(L), strtab, strtab_size * sizeof(char));
  if(section_headers != NULL)
    lj_mem_free(G(L), section_headers, shnum * shentsize);

  fclose(elf_file);

  return status;
}

static int dump_dyn_symtab(struct dl_phdr_info *info, const uint8_t header,
			   struct lj_wbuf *buf)
{
  size_t header_index;
  for (header_index = 0; header_index < info->dlpi_phnum; ++header_index) {
    if (info->dlpi_phdr[header_index].p_type == PT_DYNAMIC) {
      ElfW(Dyn *) dyn =
	(ElfW(Dyn) *)(info->dlpi_addr + info->dlpi_phdr[header_index].p_vaddr);
      ElfW(Sym *) sym = NULL;
      ElfW(Word *) hashtab = NULL;
      ElfW(Addr) ghashtab = 0;
      ElfW(Word) sym_cnt = 0;

      char *strtab = 0;

      for(; dyn->d_tag != DT_NULL; dyn++) {
        switch(dyn->d_tag) {
        case DT_HASH:
          hashtab = (ElfW(Word *))dyn->d_un.d_ptr;
          break;
        case DT_GNU_HASH:
          ghashtab = dyn->d_un.d_ptr;
          break;
        case DT_STRTAB:
          strtab = (char *)dyn->d_un.d_ptr;
          break;
        case DT_SYMTAB:
          sym = (ElfW(Sym *))dyn->d_un.d_ptr;
          break;
        default:
          break;
        }
      }

      if ((hashtab == NULL && ghashtab == 0) || strtab == NULL || sym == NULL)
        /* Not enough data to resolve symbols. */
        return 1;

      /*
      ** A hash table consists of Elf32_Word or Elf64_Word objects that provide
      ** for symbol table access. Hash table has the following organization:
      ** +-------------------+
      ** |      nbucket      |
      ** +-------------------+
      ** |      nchain       |
      ** +-------------------+
      ** |     bucket[0]     |
      ** |       ...         |
      ** | bucket[nbucket-1] |
      ** +-------------------+
      ** |     chain[0]      |
      ** |       ...         |
      ** |  chain[nchain-1]  |
      ** +-------------------+
      ** Chain table entries parallel the symbol table. The number of symbol
      ** table entries should equal nchain, so symbol table indexes also select
      ** chain table entries. Since the chain array values are indexes for not
      ** only the chain array itself, but also for the symbol table, the chain
      ** array must be the same size as the symbol table. This makes nchain
      ** equal to the length of the symbol table.
      **
      ** For more, see https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-48031.html
      */
      sym_cnt = ghashtab == 0 ? hashtab[1] : ghashtab_size(ghashtab);
      write_c_symtab(sym, strtab, info->dlpi_addr, sym_cnt, header, buf);
      return 0;
    }
  }

  return 1;
}

struct symbol_resolver_conf {
  struct lj_wbuf *buf; /* Output buffer. */
  lua_State *L; /* Current Lua state. */
  const uint8_t header; /* Header for symbol entries to write. */

  uint32_t cur_lib; /* Index of the lib currently dumped. */
  uint32_t to_dump_cnt; /* Amount of libs to dump. */
  uint32_t *lib_adds; /* Memprof's counter of libs. */
};

static int resolve_symbolnames(struct dl_phdr_info *info, size_t info_size,
			       void *data)
{
  struct symbol_resolver_conf *conf = data;
  struct lj_wbuf *buf = conf->buf;
  lua_State *L = conf->L;
  const uint8_t header = conf->header;
  char executable_path[PATH_MAX] = {0};

  uint32_t lib_cnt = 0;

  /*
  ** Check that dlpi_adds and dlpi_subs fields are available.
  ** Assertion was taken from the GLIBC tests:
  ** https://code.woboq.org/userspace/glibc/elf/tst-dlmodcount.c.html#37
  */
  lj_assertL(info_size > offsetof(struct dl_phdr_info, dlpi_subs)
			 + sizeof(info->dlpi_subs),
	     "bad dlpi_subs");

  lib_cnt = info->dlpi_adds - *conf->lib_adds;

  /* Skip vDSO library. */
  if (info->dlpi_addr == getauxval(AT_SYSINFO_EHDR))
    return 0;

  if ((conf->to_dump_cnt = info->dlpi_adds - *conf->lib_adds) == 0)
    /* No new libraries, stop resolver. */
    return 1;

  if (conf->cur_lib < lib_cnt - conf->to_dump_cnt) {
    /* That lib is already dumped, skip it. */
    ++conf->cur_lib;
    return 0;
  }

  if (conf->cur_lib == lib_cnt - conf->to_dump_cnt - 1)
    /* Last library, update memrpof's lib counter. */
    *conf->lib_adds = info->dlpi_adds;

  /*
  ** The `dl_iterate_phdr` returns an empty string as a name for
  ** the executable from which it was called. It is still possible
  ** to access its dynamic symbol table, but it is vital for
  ** sysprof to obtain the main symbol table for the LuaJIT
  ** executable. To do so, we need a valid path to the executable.
  ** Since there is no way to obtain the path to a running
  ** executable using the C standard library, the only more or
  ** less reliable way to do this is by reading the symbolic link
  ** from `/proc/self/exe`. Most of the UNIX-based systems have
  ** procfs, so it is not a problem.
  ** Such path tweaks relate only for the main way (see below).
  */
  if (*info->dlpi_name == '\0') {
    if (readlink("/proc/self/exe", executable_path, PATH_MAX) != -1)
      info->dlpi_name = executable_path;
    else
      /*
      ** It is impossible for sysprof to work properly without the
      ** LuaJIT's .symtab section present. The assertion below is
      ** unlikely to be triggered on any system supported by
      ** sysprof, unless someone have deleted the LuaJIT binary
      ** right after the start.
      */
      lj_assertL(0, "bad executed binary symtab section");
  }

  /*
  ** Main way: try to open ELF and read SHT_SYMTAB, SHT_STRTAB and SHT_HASH
  ** sections from it.
  */
  if (dump_sht_symtab(info->dlpi_name, buf, L, header, info->dlpi_addr) == 0) {
    ++conf->cur_lib;
  }
  /* First fallback: dump functions only from PT_DYNAMIC segment. */
  else if(dump_dyn_symtab(info, header, buf) == 0) {
    ++conf->cur_lib;
  }
  /*
  ** Last resort: dump ELF size and address to show .so name for its functions
  ** in memprof output.
  */
  else {
    lj_wbuf_addbyte(buf, SYMTAB_CFUNC);
    lj_wbuf_addu64(buf, info->dlpi_addr);
    lj_wbuf_addstring(buf, info->dlpi_name);
    ++conf->cur_lib;
  }

  return 0;
}

#endif /* LJ_HASRESOLVER */

void lj_symtab_dump_newc(uint32_t *lib_adds, struct lj_wbuf *out,
			 uint8_t header, struct lua_State *L) {
#if LJ_HASRESOLVER
  struct symbol_resolver_conf conf = {
    .buf = out,
    .L = L,
    .header = header,
    .cur_lib = 0,
    .to_dump_cnt = 0,
    .lib_adds = lib_adds
  };
  dl_iterate_phdr(resolve_symbolnames, &conf);
#else
  UNUSED(lib_adds);
  UNUSED(out);
  UNUSED(header);
  UNUSED(L);
#endif
}

void lj_symtab_dump(struct lj_wbuf *out, const struct global_State *g,
		    uint32_t *lib_adds)
{
  const GCRef *iter = &g->gc.root;
  const GCobj *o;
  const size_t ljs_header_len = sizeof(ljs_header) / sizeof(ljs_header[0]);

#if LJ_HASRESOLVER
  struct symbol_resolver_conf conf = {
    .buf = out,
    .L = gco2th(gcref(g->cur_L)),
    .header = SYMTAB_CFUNC,
    .cur_lib = 0,
    .to_dump_cnt = 0,
    .lib_adds = lib_adds
  };
#else
  UNUSED(lib_adds);
#endif

  /* Write prologue. */
  lj_wbuf_addn(out, ljs_header, ljs_header_len);

  while ((o = gcref(*iter)) != NULL) {
    switch (o->gch.gct) {
    case (~LJ_TPROTO): {
      const GCproto *pt = gco2pt(o);
      lj_wbuf_addbyte(out, SYMTAB_LFUNC);
      lj_symtab_dump_proto(out, pt);
      break;
    }
#if LJ_HASJIT
    case (~LJ_TTRACE): {
      lj_wbuf_addbyte(out, SYMTAB_TRACE);
      lj_symtab_dump_trace(out, gco2trace(o));
      break;
    }
#endif /* LJ_HASJIT */
    default:
      break;
    }
    iter = &o->gch.nextgc;
  }

#if LJ_HASRESOLVER
  /* Write C symbols. */
  dl_iterate_phdr(resolve_symbolnames, &conf);
#endif
  lj_wbuf_addbyte(out, SYMTAB_FINAL);
}
