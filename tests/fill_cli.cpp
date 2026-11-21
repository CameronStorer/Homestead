// Fills the Homestead placeholder in an IPS file using the app's own code.
// Usage: fill_cli <in.ips> <out.ips> <server>
#include "../app/source/homestead/ips.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: fill_cli in.ips out.ips server\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> ips((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    int n = Homestead::Ips::fillSlots(ips, Homestead::Ips::PLACEHOLDER, argv[3], error);
    if (n < 0) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char *>(ips.data()), ips.size());
    std::printf("%d\n", n);
    return 0;
}
