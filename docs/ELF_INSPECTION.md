# ELF inspection

Build `gt4_elf_inspect` with CMake, then run:

```sh
./build/gt4_elf_inspect /path/to/your/legally-obtained/CORE.GT4
```

The tool prints the entry point and ELF32/MIPS loadable segments. It parses an input file provided by the user, not a file distributed by this repository.

**Limits:** this is an ELF32 little-endian MIPS executable reader, not an emulator or native compiler. It does not execute PS2 instructions, resolve relocations, load disc volumes, or map hardware. Do not run it on untrusted files; memory sizes come from ELF headers. ELF segments with overlapping virtual addresses are parsed but cannot all be mapped by the strict AddressSpace without an explicit policy.
