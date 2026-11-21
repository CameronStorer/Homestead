#pragma once

// Homestead: fills the user's server address into IPS patch templates.
//
// The real-hardware sysmodule patches are built with fixed-size string slots
// that hold PLACEHOLDER (for example "https://homestead.invalid/ac/" followed
// by NUL padding). At install time the app copies each template IPS and
// replaces PLACEHOLDER inside every slot with the user's server, keeping the
// slot size unchanged so no code or data after the slot moves.
//
// Plain C++ with no libctru dependency so it can be unit-tested on a PC.

#include <cstdint>
#include <string>
#include <vector>

namespace Homestead::Ips {
    constexpr const char *PLACEHOLDER = "homestead.invalid";

    // Replaces every occurrence of `placeholder` inside the data of `ips` with
    // `value`. Each occurrence must sit in a slot: the C string that contains
    // it, plus the NUL bytes that follow it inside the same IPS record. The
    // new string (and its NUL) must fit in that slot. Returns the number of
    // slots filled, or -1 on error with `error` set.
    int fillSlots(std::vector<uint8_t> &ips, const std::string &placeholder,
                  const std::string &value, std::string &error);

    // Checks the "PATCH" ... "EOF" structure without changing anything.
    bool validate(const std::vector<uint8_t> &ips, std::string &error);
}
