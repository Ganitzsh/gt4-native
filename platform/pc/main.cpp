#include <iostream>
#include <string_view>
int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--self-test") return 0;
    std::cout << "GT4 Native bootstrap - game runtime not yet integrated.\n";
    return 0;
}