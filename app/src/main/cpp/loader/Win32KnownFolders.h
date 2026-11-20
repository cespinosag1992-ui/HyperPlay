#pragma once

// Mapeo universal de FOLDERID_* / CSIDL_* a rutas Win32 del árbol del juego.
// Header-only para que la suite nativa del dispositivo valide exactamente el mismo
// mapeo que usa SHGetKnownFolderPath / SHGetFolderPathW.

#include <cstdint>
#include <cstring>
#include <string>

namespace hyperplay::win32 {

inline std::string knownFolderFromGuid(const void* rfid) noexcept {
    if (rfid == nullptr) return "C:\\users\\default";
    const auto* g = static_cast<const uint8_t*>(rfid);
    const uint32_t data1 = static_cast<uint32_t>(g[0]) | (static_cast<uint32_t>(g[1]) << 8) |
                           (static_cast<uint32_t>(g[2]) << 16) | (static_cast<uint32_t>(g[3]) << 24);
    switch (data1) {
        case 0x5E6C858F: return "C:\\users\\default";                   // FOLDERID_Profile
        case 0xF1B32785: return "C:\\users\\default\\AppData\\Local";    // FOLDERID_LocalAppData
        case 0x3EB685DB: return "C:\\users\\default\\AppData\\Roaming";  // FOLDERID_RoamingAppData
        case 0xA520A1A4: return "C:\\users\\default\\AppData\\LocalLow"; // FOLDERID_LocalLow
        case 0xFDD39AD0: return "C:\\users\\default\\Documents";        // FOLDERID_Documents
        case 0x4C5C32FF: return "C:\\users\\default\\Saved Games";      // FOLDERID_SavedGames
        case 0x62AB5D82: return "C:\\ProgramData";                      // FOLDERID_ProgramData
        case 0xB4BFCC3A: return "C:\\users\\default\\Desktop";          // FOLDERID_Desktop
        case 0xF38BF404: return "C:\\Windows";                          // FOLDERID_Windows
        case 0x1AC14E77: return "C:\\Windows\\System32";                // FOLDERID_System
        default:         return "C:\\users\\default\\AppData\\LocalLow";
    }
}

inline std::string knownFolderFromCsidl(int32_t csidl) noexcept {
    switch (csidl & 0xFF) {
        case 0x001A: return "C:\\users\\default\\AppData\\Roaming";  // CSIDL_APPDATA
        case 0x001C: return "C:\\users\\default\\AppData\\Local";    // CSIDL_LOCAL_APPDATA
        case 0x0005: return "C:\\users\\default\\Documents";        // CSIDL_MYDOCUMENTS
        case 0x0028: return "C:\\users\\default";                   // CSIDL_PROFILE
        case 0x0023: return "C:\\ProgramData";                      // CSIDL_COMMON_APPDATA
        case 0x0000: return "C:\\users\\default\\Desktop";          // CSIDL_DESKTOP
        case 0x0024: return "C:\\Windows";                          // CSIDL_WINDOWS
        case 0x0025: return "C:\\Windows\\System32";                 // CSIDL_SYSTEM
        default:     return "C:\\users\\default\\AppData\\LocalLow";
    }
}

}  // namespace hyperplay::win32
