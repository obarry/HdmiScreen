// =============================================================================
// ComUtils.h — Outils communs pour COM, HRESULT et handles Windows
// =============================================================================
//
// Notions expliquées dans docs/API_WINDOWS.md §2 (COM) et
// docs/CPP_POUR_DEV_JAVA.md §3 (RAII) et §6 (exceptions vs codes d'erreur).

#pragma once

// [API] Réduit la taille de <windows.h> (exclut des API anciennes) et empêche
// les macros min/max de Windows d'entrer en conflit avec std::min/std::max.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// [API] ComPtr : pointeur intelligent pour objets COM (fourni par le SDK
// Windows, en-tête seul). Il appelle AddRef/Release automatiquement.
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace hdmi {

// [C++] « using » crée un alias de type : on écrira ComPtr<IMFSample> au lieu
// de Microsoft::WRL::ComPtr<IMFSample>. (Un « template » est un type
// paramétré, proche des génériques Java, mais résolu à la compilation.)
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// -----------------------------------------------------------------------------
// Erreurs : HRESULT -> exception
// -----------------------------------------------------------------------------
// [API] Les fonctions Windows/COM ne lèvent pas d'exception : elles renvoient
// un HRESULT, entier 32 bits où « < 0 » signifie échec (macro FAILED).
// On convertit ces codes en exception C++ pour ne pas tester chaque appel à la main.
//
// [C++ vs Java] On hérite de std::runtime_error (~ RuntimeException).
// Toutes les exceptions C++ sont « unchecked » : aucune clause throws.
class HrError : public std::runtime_error {
public:
    HrError(HRESULT hr, const char* context)
        : std::runtime_error(format(hr, context)), hr_(hr) {}   // liste d'initialisation

    HRESULT code() const noexcept { return hr_; }   // noexcept : promet de ne rien lever

private:
    static std::string format(HRESULT hr, const char* context) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s (HRESULT 0x%08lX)", context,
                      static_cast<unsigned long>(hr));
        return buffer;
    }
    HRESULT hr_;
};

// Lève HrError si l'appel a échoué. Usage :
//   throwIfFailed(MFStartup(MF_VERSION), "MFStartup");
inline void throwIfFailed(HRESULT hr, const char* context) {
    if (FAILED(hr)) {
        throw HrError(hr, context);
    }
}

// Convertit un texte UTF-8 (std::string, ex. message d'exception) en UTF-16
// (std::wstring), l'encodage natif des API Windows « W ».
// [C++ vs Java] Une String Java est toujours en UTF-16. En C++, std::string
// n'est qu'une suite d'octets : l'encodage est une convention (ici UTF-8,
// grâce à l'option /utf-8 du compilateur).
inline std::wstring widen(const char* utf8) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (length <= 1) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length - 1), L'\0');   // -1 : sans le zéro final
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, result.data(), length);
    return result;
}

// -----------------------------------------------------------------------------
// RAII : initialisation et nettoyage automatiques
// -----------------------------------------------------------------------------
// [C++ vs Java] RAII = « Resource Acquisition Is Initialization ». Le
// constructeur acquiert la ressource, le DESTRUCTEUR (~Classe) la libère.
// Le destructeur s'exécute de façon déterministe quand l'objet sort de sa
// portée, même en cas d'exception : c'est un try-with-resources implicite.

// Initialise COM pour le thread courant (obligatoire avant tout appel COM).
class ComInitializer {
public:
    ComInitializer() {
        // [API] COINIT_APARTMENTTHREADED : thread « STA », requis pour un thread
        // qui possède des fenêtres. Voir docs/API_WINDOWS.md §2.
        throwIfFailed(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE),
                      "CoInitializeEx");
    }
    ~ComInitializer() { CoUninitialize(); }

    // [C++] Interdire la copie : deux copies appelleraient CoUninitialize deux fois.
    // En Java la question ne se pose pas : les objets ne sont jamais copiés implicitement.
    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
};

// Possède un HANDLE Windows (événement, fichier…) et le ferme à la destruction.
class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    HANDLE get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    void reset(HANDLE handle = nullptr) noexcept {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_;
};

// Libère une chaîne allouée par COM (CoTaskMemAlloc) quand elle sort de portée.
// Utilisé avec IMFActivate::GetAllocatedString.
class CoTaskString {
public:
    CoTaskString() = default;
    ~CoTaskString() { CoTaskMemFree(text_); }
    CoTaskString(const CoTaskString&) = delete;
    CoTaskString& operator=(const CoTaskString&) = delete;

    // Adresse du pointeur, à passer aux API qui « remplissent » la chaîne.
    // [C++] wchar_t** : pointeur vers un pointeur ; l'API écrit dans notre variable.
    wchar_t** put() noexcept { return &text_; }
    std::wstring str() const { return text_ ? std::wstring(text_) : std::wstring(); }

private:
    wchar_t* text_ = nullptr;
};

} // namespace hdmi
