#pragma once
// GT4's progress from a PS2 into the native save folder (docs/SAVES.md).
//
// The native game keeps its saves as plain files in a folder (runtime/memcard.cpp serves them to the game's own
// save code; by default %LOCALAPPDATA%\gt4-native\saves): one folder per save the game made (GT4's names, such as
// BASCUS-97328GAMEDATA for the game data and garage), its files inside. A save made on a PS2 or in PCSX2 comes in
// with importSave from:
// - a memory card image: PCSX2's .ps2 (8 MiB of 512-byte pages, with their 16 bytes of ECC or without), the raw
//   dump of a card (.bin, .mcd), read through the card's own file system;
// - a PCSX2 folder memory card (a folder with the save folders in it);
// - a single save exported as .psu (uLaunchELF, PCSX2's memory card editor).
// Only GT4's (NTSC-U) saves are taken, BASCUS-97328*; a save already in the folder is moved to
// .backup/imports/DATE/NAME inside it, where the game does not see it (runtime/save_folder.h hides names
// starting with ".").
#include <filesystem>
#include <string>
#include <vector>

namespace gt4::saves {

struct ImportResult {
    bool ok = false;
    std::string message;                 // what was done, or why not
    std::vector<std::string> imported;   // the save folders written
};

// The default save folder (%LOCALAPPDATA%\gt4-native\saves, or ./saves).
std::filesystem::path defaultSaveFolder();

ImportResult importSave(const std::filesystem::path& source, const std::filesystem::path& saveFolder);

// The saves a source holds (folder name -> its files), without writing anything; empty with `error` set when the
// source cannot be read.
struct SaveEntry {
    std::string folder;
    std::vector<std::pair<std::string, std::vector<unsigned char>>> files;
};
std::vector<SaveEntry> readSaves(const std::filesystem::path& source, std::string& error);

// One save of the folder (its name, such as BASCUS-97328GAMEDATA) as a .psu, which PCSX2's memory card editor,
// uLaunchELF on a PS2 and importSave read. `out` is written whole or not at all.
struct ExportResult {
    bool ok = false;
    std::string message;
};
ExportResult exportSave(const std::filesystem::path& saveFolder, const std::string& save, const std::filesystem::path& out);

}  // namespace gt4::saves
