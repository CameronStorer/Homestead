#include "homestead.hpp"
#include "ips.hpp"
#include "../states/MainUI.hpp"
#include "../sysmodules/httpc.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <format>
#include <sys/stat.h>
#include <vector>

namespace Homestead {
    namespace {
        std::string currentServer;

        constexpr Result ResultAlreadyExists = MAKERESULT(RL_STATUS, RS_INVALIDARG, RM_HTTP, RD_ALREADY_EXISTS);
        constexpr Result ResultNoAzaharCommand = MAKERESULT(RL_PERMANENT, RS_WRONGARG, RM_OS, 47);
        constexpr Result ResultNoServer = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, RD_NOT_INITIALIZED);
        constexpr Result ResultPatchesMissing = MAKERESULT(RL_PERMANENT, RS_NOTFOUND, RM_APPLICATION, RD_NOT_FOUND);
        constexpr Result ResultPatchFailed = MAKERESULT(RL_PERMANENT, RS_CANCELED, RM_APPLICATION, RD_NO_DATA);

        // Every Nintendo host is sent to the one server address, so a
        // personal server needs no wildcard DNS and can be a bare IP. The
        // server tells services apart by URL path.
        constexpr std::array<const char *, 3> AZAHAR_PATTERNS = {{
            "^.*nintendowifi\\.net$",
            "^.*nintendo\\.net$",
            "^.*pokemon-gl\\.com$",
        }};

        // Real-hardware patch templates bundled in the app (built from the
        // sysmodule patches with PLACEHOLDER slots, see patches/README.md).
        struct PatchFile {
            const char *titleId;
        };
        constexpr std::array<PatchFile, 5> PATCH_FILES = {{
            {"0004013000003202"}, // friends
            {"0004013000003802"}, // act
            {"0004013000002902"}, // http
            {"0004013000002E02"}, // socket
            {"0004013000002F02"}, // ssl
        }};
        constexpr const char *PATCH_TEMPLATE_DIR = "romfs:/homestead_ips/";
        constexpr const char *PATCH_TARGET_DIR = "sdmc:/luma/sysmodules/";
        constexpr const char *PATCH_BACKUP_DIR = "sdmc:/3ds/homestead/ips-backup/";

        bool isAzahar() {
            s64 emu_id = 0;
            svcGetSystemInfo(&emu_id, 0x20000, 0);
            return emu_id == 2;
        }

        bool readFile(const std::string &path, std::vector<u8> &data) {
            FILE *f = std::fopen(path.c_str(), "rb");
            if (!f) return false;
            data.clear();
            u8 buffer[2048];
            size_t n;
            while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
                data.insert(data.end(), buffer, buffer + n);
            }
            const bool ok = !std::ferror(f);
            std::fclose(f);
            return ok;
        }

        bool writeFile(const std::string &path, const std::vector<u8> &data) {
            const std::string tmp = path + ".tmp";
            FILE *f = std::fopen(tmp.c_str(), "wb");
            if (!f) return false;
            const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
            if (std::fclose(f) != 0 || !ok) {
                std::remove(tmp.c_str());
                return false;
            }
            std::remove(path.c_str());
            return std::rename(tmp.c_str(), path.c_str()) == 0;
        }

        bool fileExists(const std::string &path) {
            struct stat st;
            return stat(path.c_str(), &st) == 0;
        }

        Result registerRule(const char *pattern, const std::string &replacement) {
            const size_t len = std::char_traits<char>::length(pattern);
            Result res = HTTPC_Azahar_RegisterURLReplacement(pattern, replacement.c_str(), len, replacement.size());
            if (res == ResultAlreadyExists) {
                res = HTTPC_Azahar_UnregisterURLReplacement(pattern, len);
                if (R_SUCCEEDED(res)) {
                    res = HTTPC_Azahar_RegisterURLReplacement(pattern, replacement.c_str(), len, replacement.size());
                }
            }
            return res;
        }

        // Azahar with HLE HTTP: register or clear the Homestead URL rules.
        // Returns ResultNoAzaharCommand when HTTP runs as an LLE module.
        Result applyAzaharRules(u8 friend_account_id) {
            Result res = httpcInit(0x1000);
            if (R_FAILED(res)) return res;

            for (const char *pattern : AZAHAR_PATTERNS) {
                if (friend_account_id == ACCOUNT_ID) {
                    res = registerRule(pattern, currentServer);
                } else {
                    res = HTTPC_Azahar_UnregisterURLReplacement(pattern, std::char_traits<char>::length(pattern));
                    if (R_SUCCEEDED(res)) res = 0; // "was not registered" is fine
                }
                if (R_FAILED(res)) break;
            }
            httpcExit();
            return res;
        }

        // Real hardware (or Azahar with LLE HTTP): install the sysmodule
        // patches with the server address filled in. The patches only
        // redirect the Dev-environment account, so Nintendo and Pretendo
        // accounts keep working with them installed.
        Result installPatches(MainStruct *mainStruct) {
            mkdir("sdmc:/luma", 0777);
            mkdir("sdmc:/luma/sysmodules", 0777);
            mkdir(DATA_DIR, 0777);
            mkdir(PATCH_BACKUP_DIR, 0777);

            // Load and fill every template first, so a failure changes nothing.
            std::vector<std::vector<u8>> filled;
            for (const PatchFile &file : PATCH_FILES) {
                std::vector<u8> ips;
                if (!readFile(std::string(PATCH_TEMPLATE_DIR) + file.titleId + ".ips", ips)) {
                    LOG_NIMBUS_ERROR(mainStruct, "This build of Homestead has no real-hardware patches yet. The custom server currently works on Azahar only.");
                    return ResultPatchesMissing;
                }
                std::string error;
                if (Ips::fillSlots(ips, Ips::PLACEHOLDER, currentServer, error) < 0) {
                    LOGF_NIMBUS_ERROR(mainStruct, "Patch %s: %s", file.titleId, error.c_str());
                    return ResultPatchFailed;
                }
                filled.push_back(std::move(ips));
            }

            for (size_t i = 0; i < PATCH_FILES.size(); i++) {
                const std::string name = std::string(PATCH_FILES[i].titleId) + ".ips";
                const std::string target = PATCH_TARGET_DIR + name;
                const std::string backup = PATCH_BACKUP_DIR + name;
                // Keep whatever was installed before Homestead (usually
                // Nimbus) once, so it can be put back by hand.
                if (fileExists(target) && !fileExists(backup)) {
                    std::vector<u8> old;
                    if (readFile(target, old)) writeFile(backup, old);
                }
                if (!writeFile(target, filled[i])) {
                    LOGF_NIMBUS_ERROR(mainStruct, "Could not write %s", target.c_str());
                    return ResultPatchFailed;
                }
            }
            return 0;
        }

        bool isIpv4(const std::string &s) {
            int parts = 0;
            size_t i = 0;
            while (i <= s.size()) {
                size_t j = i;
                int value = 0;
                while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
                    value = value * 10 + (s[j] - '0');
                    if (value > 255 || j - i >= 3) return false;
                    j++;
                }
                if (j == i) return false;
                parts++;
                if (j == s.size()) break;
                if (s[j] != '.') return false;
                i = j + 1;
            }
            return parts == 4;
        }
    }

    const std::string &server() { return currentServer; }

    void loadServer() {
        currentServer.clear();
        FILE *f = std::fopen(SERVER_FILE, "rb");
        if (!f) return;
        char buffer[MAX_SERVER_LENGTH + 2] = {};
        const size_t n = std::fread(buffer, 1, sizeof(buffer) - 1, f);
        std::fclose(f);
        std::string address(buffer, n);
        while (!address.empty() && std::isspace(static_cast<unsigned char>(address.back()))) address.pop_back();
        std::string error;
        if (normalizeServer(address, error)) currentServer = address;
    }

    bool saveServer(const std::string &address) {
        mkdir(DATA_DIR, 0777);
        std::vector<u8> data(address.begin(), address.end());
        data.push_back('\n');
        if (!writeFile(SERVER_FILE, data)) return false;
        currentServer = address;
        return true;
    }

    bool normalizeServer(std::string &address, std::string &error) {
        // Accept a pasted URL by dropping the scheme and any path.
        if (auto pos = address.find("://"); pos != std::string::npos) address.erase(0, pos + 3);
        if (auto pos = address.find('/'); pos != std::string::npos) address.erase(pos);
        for (char &c : address) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (address.empty()) {
            error = "Enter a server address.";
            return false;
        }
        if (address.size() > MAX_SERVER_LENGTH) {
            error = std::format("The address is too long ({} characters max).", MAX_SERVER_LENGTH);
            return false;
        }
        if (address.find(':') != std::string::npos) {
            error = "Ports are not supported; the server must listen on the standard ports.";
            return false;
        }
        if (isIpv4(address)) return true;

        // Hostname: labels of a-z, 0-9 and '-', not starting or ending with '-'.
        size_t labelStart = 0;
        for (size_t i = 0; i <= address.size(); i++) {
            if (i == address.size() || address[i] == '.') {
                const size_t len = i - labelStart;
                if (len == 0 || len > 63 || address[labelStart] == '-' || address[i - 1] == '-') {
                    error = "That is not a valid hostname.";
                    return false;
                }
                labelStart = i + 1;
                continue;
            }
            const char c = address[i];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) {
                error = "Only letters, numbers, '-' and '.' are allowed.";
                return false;
            }
        }
        return true;
    }

    bool promptServer(MainStruct *mainStruct) {
        SwkbdState swkbd;
        char buffer[MAX_SERVER_LENGTH + 1] = {};
        swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, MAX_SERVER_LENGTH);
        swkbdSetHintText(&swkbd, "Server address, e.g. isle.example.org");
        swkbdSetInitialText(&swkbd, currentServer.c_str());
        swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT | SWKBD_DEFAULT_QWERTY);
        if (swkbdInputText(&swkbd, buffer, sizeof(buffer)) != SWKBD_BUTTON_CONFIRM) {
            return false;
        }

        std::string address(buffer);
        std::string error;
        if (!normalizeServer(address, error)) {
            LOG_NIMBUS_ERROR(mainStruct, error.c_str());
            return false;
        }
        if (!saveServer(address)) {
            LOG_NIMBUS_ERROR(mainStruct, "Could not save the server address to the SD card.");
            return false;
        }
        LOGF_NIMBUS_ERROR(mainStruct, "Custom server set to %s", address.c_str());
        return true;
    }

    NascEnvironment environmentOf(u8 friend_account_id) {
        if (friend_account_id == ACCOUNT_ID) return ENVIRONMENT;
        return friend_account_id == 2 ? NascEnvironment::NASC_ENV_Test : NascEnvironment::NASC_ENV_Prod;
    }

    Result applyRouting(MainStruct *mainStruct, u8 friend_account_id) {
        // Nimbus's own handling: Pretendo rules on for account 2, off otherwise.
        Result res = MainUI::handleAzahar(friend_account_id);
        if (R_FAILED(res)) return res;

        if (friend_account_id == ACCOUNT_ID && currentServer.empty()) {
            LOG_NIMBUS_ERROR(mainStruct, "Set a server address first (press SELECT).");
            return ResultNoServer;
        }

        if (isAzahar()) {
            res = applyAzaharRules(friend_account_id);
            if (res != ResultNoAzaharCommand) return res;
            // LLE HTTP on Azahar: fall through to the IPS patches.
        }

        // The patches redirect only the Dev-environment account, so they are
        // only (re)written when the custom server is chosen.
        if (friend_account_id != ACCOUNT_ID) return 0;
        return installPatches(mainStruct);
    }
}
