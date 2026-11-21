// Host-side tests for app/source/homestead/ips.cpp.
// Build and run: tests/run.sh
#include "../app/source/homestead/ips.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Homestead::Ips::fillSlots;
using Homestead::Ips::PLACEHOLDER;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                   \
        }                                                                 \
    } while (0)

// Builds an IPS with one literal record at `offset` holding `data`.
static std::vector<uint8_t> makeIps(uint32_t offset, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> ips = {'P', 'A', 'T', 'C', 'H'};
    ips.push_back(offset >> 16);
    ips.push_back(offset >> 8);
    ips.push_back(offset);
    ips.push_back(data.size() >> 8);
    ips.push_back(data.size());
    ips.insert(ips.end(), data.begin(), data.end());
    ips.insert(ips.end(), {'E', 'O', 'F'});
    return ips;
}

// A slot: `text` then NULs up to `slotSize` bytes.
static std::vector<uint8_t> slot(const std::string &text, size_t slotSize) {
    std::vector<uint8_t> out(text.begin(), text.end());
    out.resize(slotSize, 0);
    return out;
}

static std::string slotText(const std::vector<uint8_t> &ips, size_t at) {
    return std::string(reinterpret_cast<const char *>(ips.data() + at));
}

int main() {
    const std::string ph = PLACEHOLDER;
    std::string err;

    // URL slot with prefix and suffix.
    {
        auto data = slot("https://" + ph + "/ac/", 64);
        auto ips = makeIps(0x6129A, data);
        CHECK(fillSlots(ips, ph, "100.99.192.29", err) == 1);
        CHECK(slotText(ips, 10) == "https://100.99.192.29/ac/");
        CHECK(ips[10 + 63] == 0);
        CHECK(ips.size() == data.size() + 13); // file size unchanged
    }

    // Two slots in one record separated by code bytes, plus surrounding code.
    {
        std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x00};
        auto a = slot(ph, 32);
        auto b = slot("https://" + ph + "/v1/api/", 64);
        data.insert(data.end(), a.begin(), a.end());
        data.push_back(0xE1); // non-NUL byte ends slot a
        data.insert(data.end(), b.begin(), b.end());
        data.push_back(0xAA);
        auto ips = makeIps(0x10E4F8, data);
        CHECK(fillSlots(ips, ph, "isle.example.org", err) == 2);
        CHECK(slotText(ips, 10 + 4) == "isle.example.org");
        CHECK(ips[10 + 4 + 32] == 0xE1);
        CHECK(slotText(ips, 10 + 4 + 33) == "https://isle.example.org/v1/api/");
        CHECK(ips[10 + 4 + 33 + 64] == 0xAA);
        CHECK(ips[10] == 0x01 && ips[11] == 0x02 && ips[12] == 0x03);
    }

    // Longest value that fits, then one character too many.
    {
        auto ips = makeIps(0x100, slot(ph, 20));
        CHECK(fillSlots(ips, ph, std::string(19, 'a'), err) == 1);
        auto ips2 = makeIps(0x100, slot(ph, 20));
        CHECK(fillSlots(ips2, ph, std::string(20, 'a'), err) == -1);
        CHECK(err.find("too long") != std::string::npos);
    }

    // Shorter value clears the old tail.
    {
        auto ips = makeIps(0x100, slot("https://" + ph + "/ac/", 48));
        CHECK(fillSlots(ips, ph, "a.io", err) == 1);
        CHECK(slotText(ips, 10) == "https://a.io/ac/");
        for (size_t k = 10 + 16; k < 10 + 48; k++) CHECK(ips[k] == 0);
    }

    // No placeholder: nothing filled, file untouched.
    {
        auto ips = makeIps(0x100, slot("https://account.pretendo.cc/v1/api/", 40));
        auto copy = ips;
        CHECK(fillSlots(ips, ph, "x.y", err) == 0);
        CHECK(ips == copy);
    }

    // RLE records are skipped and parsing continues after them.
    {
        std::vector<uint8_t> ips = {'P', 'A', 'T', 'C', 'H', 0, 0, 0x10, 0, 0, 0, 8, 0xFF};
        auto rec = makeIps(0x200, slot(ph, 24));
        ips.insert(ips.end(), rec.begin() + 5, rec.end());
        CHECK(fillSlots(ips, ph, "rle.test", err) == 1);
    }

    // Malformed files are rejected.
    {
        std::vector<uint8_t> bad = {'N', 'O', 'P', 'E', '!', 'E', 'O', 'F'};
        CHECK(fillSlots(bad, ph, "x", err) == -1);
        auto cut = makeIps(0x100, slot(ph, 24));
        cut.resize(cut.size() - 10);
        CHECK(fillSlots(cut, ph, "x", err) == -1);
    }

    // A placeholder with no NUL before the record ends is an error.
    {
        std::vector<uint8_t> data(ph.begin(), ph.end());
        auto ips = makeIps(0x100, data);
        CHECK(fillSlots(ips, ph, "x", err) == -1);
    }

    if (failures == 0) std::printf("ips_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
