#pragma once

#include <filesystem>
#include <string>

// The game executable the native game loads (CORE.GT4.elf), made from the user's own disc image the way
// gt4-decomp's tools do it (tools/iso_extract.py, then tools/core2elf.py): CORE.GT4 is read from the image's ISO 9660
// file system, checked against the one supported release (Gran Turismo 4, USA, SCUS-97328 version 1.01: SHA-1
// 30031137ce494323816bb707b90d3ec0d07937d2, as gt4-decomp's orig/README.md lists it), inflated, and written as the same
// ELF core2elf.py writes (SHA-1 ca47e116503e12b2db95aa7ee7debaaa99870c8b).
//
// Returns an empty string on success, else what went wrong, in words for the player.
std::string makeCoreElf(const std::filesystem::path& iso, const std::filesystem::path& elf);
