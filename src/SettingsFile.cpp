// =============================================================================
// SettingsFile.cpp — Réglages persistants dans hdmiscreen.ini
// =============================================================================

#include "SettingsFile.h"
#include "ComUtils.h"
#include "Log.h"

#include <array>

namespace hdmi {

namespace {

constexpr wchar_t kCapture[] = L"capture";
constexpr wchar_t kDisplay[] = L"display";

// Lit une valeur texte, ou « fallback » si elle est absente.
std::wstring readValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                       const wchar_t* fallback) {
    // [C++] std::array : tableau de taille fixe connue à la compilation, sur la
    // pile, sans allocation (~ un new char[512] Java, mais libéré automatiquement).
    std::array<wchar_t, 512> buffer{};
    // [API] Renvoie le nombre de caractères copiés ; « fallback » est copié si
    // la clé ou le fichier n'existe pas.
    const DWORD length = GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                                                  static_cast<DWORD>(buffer.size()), path.c_str());
    return std::wstring(buffer.data(), length);
}

void writeValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                const std::wstring& value) {
    if (!WritePrivateProfileStringW(section, key, value.c_str(), path.c_str())) {
        log::error(std::wstring(L"Écriture impossible dans ") + path);
    }
}

// Crée le fichier vide, encodé en UTF-16, s'il n'existe pas encore.
//
// [API] Subtilité des fonctions *PrivateProfile* : si le fichier n'existe pas,
// elles le créent dans l'encodage ANSI de Windows, et un nom de périphérique
// accentué (« Caméra virtuelle ») serait abîmé. Si le fichier commence par la
// marque d'ordre des octets UTF-16 (BOM, octets FF FE), elles écrivent en
// Unicode. On crée donc nous-mêmes le fichier avec ce BOM.
void createUnicodeFileIfMissing(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;   // le fichier existe déjà
    }
    // CREATE_NEW : échoue si le fichier existe (pas d'écrasement par accident).
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return;
    }
    const unsigned char bom[] = {0xFF, 0xFE};
    DWORD written = 0;
    WriteFile(file.get(), bom, sizeof(bom), &written, nullptr);
}   // UniqueHandle ferme le fichier ici (RAII)

} // namespace

std::wstring defaultSettingsPath() {
    // [API] GetModuleFileNameW(nullptr) : chemin complet de notre propre .exe.
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    std::wstring path(buffer.data(), length);
    // Remplacer l'extension : « ...\hdmiscreen.exe » -> « ...\hdmiscreen.ini ».
    const auto dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        path.resize(dot);
    }
    return path + L".ini";
}

Options loadSettings(const std::wstring& path) {
    Options options;   // valeurs par défaut

    options.deviceFilter = readValue(path, kCapture, L"device", L"");

    const std::wstring mode = readValue(path, kCapture, L"mode", L"auto");
    if (mode != L"auto") {
        options.mode = parseModeText(mode);   // texte invalide => nullopt => automatique
    }

    options.windowed = readValue(path, kDisplay, L"fullscreen", L"1") == L"0";

    if (const auto scaling = parseScalingText(readValue(path, kDisplay, L"scaling", L"fit"))) {
        // [C++17] « if (init; condition) » ou, comme ici, déclaration dans le if :
        // la variable n'existe que dans ce bloc.
        options.scaling = *scaling;
    }

    options.fullRange = readValue(path, kDisplay, L"colorRange", L"limited") == L"full";
    return options;
}

void saveSettings(const std::wstring& path, const Options& options) {
    createUnicodeFileIfMissing(path);
    writeValue(path, kCapture, L"device", options.deviceFilter);
    writeValue(path, kCapture, L"mode", options.mode ? toText(*options.mode) : L"auto");
    writeValue(path, kDisplay, L"fullscreen", options.windowed ? L"0" : L"1");
    writeValue(path, kDisplay, L"scaling", toText(options.scaling));
    writeValue(path, kDisplay, L"colorRange", options.fullRange ? L"full" : L"limited");
}

} // namespace hdmi
