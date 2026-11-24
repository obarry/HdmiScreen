// =============================================================================
// Log.cpp — Implémentation de la journalisation
// =============================================================================

#include "Log.h"
#include "ComUtils.h"

#include <format>
#include <string>

namespace hdmi::log {

namespace {
// Handle de sortie de la console, ou nullptr. Variable globale limitée à ce
// fichier (namespace anonyme), initialisée une seule fois au démarrage.
HANDLE g_console = nullptr;

void writeConsole(std::wstring_view text) {
    if (g_console == nullptr) {
        return;
    }
    DWORD written = 0;
    // [API] WriteConsoleW écrit directement de l'UTF-16 : les accents
    // s'affichent correctement quelle que soit la page de code de la console.
    WriteConsoleW(g_console, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// Heure locale au format [HH:MM:SS.mmm], pour mesurer la fréquence et la
// durée des coupures.
// [API] GetLocalTime remplit une structure SYSTEMTIME (heure, minutes…),
// l'équivalent de LocalDateTime.now() en Java.
std::wstring timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    // [C++] {:02} : sur 2 chiffres, complété par des zéros (comme %02d).
    return std::format(L"[{:02}:{:02}:{:02}.{:03}] ", now.wHour, now.wMinute, now.wSecond,
                       now.wMilliseconds);
}

void write(const wchar_t* prefix, std::wstring_view message) {
    std::wstring line = timestamp();
    line.append(prefix);
    line.append(message);   // append : concaténation sans créer de nouvelle chaîne
    line.append(L"\n");
    OutputDebugStringW(line.c_str());   // [API] flux de débogage
    writeConsole(line);
}
} // namespace

bool attachParentConsole() {
    // [API] AttachConsole(ATTACH_PARENT_PROCESS) : utilise la console de
    // l'invite qui nous a lancés. Échoue si lancé depuis l'Explorateur.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return false;
    }
    // « CONOUT$ » est le nom spécial de la sortie de la console courante.
    g_console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            0, nullptr);
    if (g_console == INVALID_HANDLE_VALUE) {
        g_console = nullptr;
        return false;
    }
    // L'invite a déjà réaffiché son prompt : on saute une ligne pour la lisibilité.
    writeConsole(L"\n");
    return true;
}

bool hasConsole() { return g_console != nullptr; }

void info(std::wstring_view message) { write(L"[HdmiScreen] ", message); }
void error(std::wstring_view message) { write(L"[HdmiScreen] ERREUR : ", message); }
void print(std::wstring_view text) { writeConsole(text); }

} // namespace hdmi::log
