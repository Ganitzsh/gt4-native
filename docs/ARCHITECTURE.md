# Native port architecture

- gt4-decomp remains authoritative for exact PS2 matching.
- Native port changes live in this repository.
- Guest PS2 addresses are 32-bit data, never directly dereferenced as host pointers.
- Host ABI and guest ABI are distinct; layout assertions do not prove functional compatibility.
- Platform APIs and source overrides must be isolated and documented.

## First integration gate
Build a small, licensed slice of real upstream runtime code with a modern compiler. Verify layouts, virtual dispatch, calling conventions, and behavior before expanding the import.

## Planned areas
compat/abi, compat/memory, platform/pc, platform/filesystem, platform/graphics, platform/audio, overrides, tools.