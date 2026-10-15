#include "gt4/elf32.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: gt4_elf_inspect <path-to-user-supplied-ELF>\n";
        return 2;
    }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) throw std::runtime_error("could not open ELF");
        std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes;
        bytes.reserve(raw.size());
        for (unsigned char b : raw) bytes.push_back(std::byte(b));
        auto image = gt4::guest::parse_mips_elf32(bytes);
        std::cout << "Entry: 0x" << std::hex << image.entry << "\n";
        std::cout << "Loadable segments: " << std::dec << image.segments.size() << "\n";
        for (const auto& segment : image.segments) {
            std::cout << "  vaddr=0x" << std::hex << segment.virtual_address
                      << " flags=0x" << segment.flags
                      << " memsz=" << std::dec << segment.bytes.size() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "ELF inspection failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
