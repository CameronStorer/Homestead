#include "ips.hpp"

#include <cstring>

namespace Homestead::Ips {
    namespace {
        struct Record {
            size_t dataStart; // index of the first data byte in the IPS file
            size_t size;      // number of data bytes (0 for RLE records)
        };

        // Walks the IPS records. RLE records (size 0) carry no literal data,
        // so they can never contain a slot and are skipped.
        bool parse(const std::vector<uint8_t> &ips, std::vector<Record> &records, std::string &error) {
            if (ips.size() < 8 || std::memcmp(ips.data(), "PATCH", 5) != 0) {
                error = "not an IPS file";
                return false;
            }
            size_t pos = 5;
            while (true) {
                if (pos + 3 > ips.size()) {
                    error = "IPS ends without EOF";
                    return false;
                }
                if (std::memcmp(ips.data() + pos, "EOF", 3) == 0) {
                    return true; // trailing bytes (truncate extension) are allowed
                }
                if (pos + 5 > ips.size()) {
                    error = "IPS record header is cut off";
                    return false;
                }
                const size_t size = (size_t(ips[pos + 3]) << 8) | ips[pos + 4];
                pos += 5;
                if (size == 0) {
                    if (pos + 3 > ips.size()) {
                        error = "IPS RLE record is cut off";
                        return false;
                    }
                    pos += 3;
                    continue;
                }
                if (pos + size > ips.size()) {
                    error = "IPS record data is cut off";
                    return false;
                }
                records.push_back({pos, size});
                pos += size;
            }
        }
    }

    bool validate(const std::vector<uint8_t> &ips, std::string &error) {
        std::vector<Record> records;
        return parse(ips, records, error);
    }

    int fillSlots(std::vector<uint8_t> &ips, const std::string &placeholder,
                  const std::string &value, std::string &error) {
        std::vector<Record> records;
        if (!parse(ips, records, error)) {
            return -1;
        }
        if (placeholder.empty()) {
            error = "empty placeholder";
            return -1;
        }

        int filled = 0;
        for (const Record &rec : records) {
            const size_t end = rec.dataStart + rec.size;
            size_t i = rec.dataStart;
            while (i + placeholder.size() <= end) {
                if (std::memcmp(ips.data() + i, placeholder.data(), placeholder.size()) != 0) {
                    i++;
                    continue;
                }

                // Find the start of the C string holding the placeholder.
                size_t strStart = i;
                while (strStart > rec.dataStart && ips[strStart - 1] != 0) {
                    strStart--;
                }
                // Find its terminating NUL inside this record.
                size_t strEnd = i + placeholder.size();
                while (strEnd < end && ips[strEnd] != 0) {
                    strEnd++;
                }
                if (strEnd == end) {
                    error = "slot string has no NUL inside its record";
                    return -1;
                }
                // The slot is the string plus every NUL that follows it.
                size_t slotEnd = strEnd;
                while (slotEnd < end && ips[slotEnd] == 0) {
                    slotEnd++;
                }

                const std::string before(reinterpret_cast<const char *>(ips.data() + strStart), i - strStart);
                const std::string after(reinterpret_cast<const char *>(ips.data() + i + placeholder.size()),
                                        strEnd - (i + placeholder.size()));
                const std::string result = before + value + after;
                const size_t slotSize = slotEnd - strStart;
                if (result.size() + 1 > slotSize) {
                    error = "server address is too long for a patch slot (" + std::to_string(slotSize - 1 - before.size() - after.size()) +
                            " characters max)";
                    return -1;
                }

                std::memset(ips.data() + strStart, 0, slotSize);
                std::memcpy(ips.data() + strStart, result.data(), result.size());
                filled++;
                i = slotEnd;
            }
        }
        return filled;
    }
}
