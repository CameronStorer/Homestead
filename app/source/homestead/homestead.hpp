#pragma once

// Homestead: the "Custom server" account on top of Nimbus.
//
// Nimbus uses friend account 1 (Nintendo, NASC environment Prod) and friend
// account 2 (Pretendo, environment Test). Homestead adds friend account 3 on
// the unused Dev environment and points it at a server address the user types
// in, so the Nintendo and Pretendo accounts are never touched.

#include "../common.hpp"
#include <string>

namespace Homestead {
    constexpr u8 ACCOUNT_ID = 3;
    constexpr NascEnvironment ENVIRONMENT = NascEnvironment::NASC_ENV_Dev;

    constexpr const char *DATA_DIR = "sdmc:/3ds/homestead";
    constexpr const char *SERVER_FILE = "sdmc:/3ds/homestead/server.txt";

    // Longest address accepted. Real-hardware patch slots are sized for this.
    constexpr size_t MAX_SERVER_LENGTH = 63;

    // The server address saved on the SD card, or "" if none is set.
    const std::string &server();
    void loadServer();
    bool saveServer(const std::string &address);

    // Accepts a hostname ("isle.example.org", "myserver") or an IPv4 address
    // ("100.99.192.29"). Lowercases `address`. Sets `error` when rejected.
    bool normalizeServer(std::string &address, std::string &error);

    // Opens the system keyboard. Returns true when the user entered a valid
    // address, which is then saved. Must be called outside a C3D frame.
    bool promptServer(MainStruct *mainStruct);

    // Friend account id -> NASC environment used when creating it.
    NascEnvironment environmentOf(u8 friend_account_id);

    // Applies the URL rules for the chosen account: Azahar's HLE URL
    // replacement when available, otherwise the real-hardware IPS patches.
    Result applyRouting(MainStruct *mainStruct, u8 friend_account_id);
}
