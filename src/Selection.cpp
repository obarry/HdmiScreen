// =============================================================================
// Selection.cpp — Implémentation du choix de périphérique et de mode
// =============================================================================

#include "Selection.h"

#include <algorithm>   // std::min, std::max_element
#include <cmath>       // std::lround
#include <cwctype>     // std::towlower
#include <format>      // std::format (C++20) ~ String.format
#include <iterator>    // std::begin, std::end
#include <tuple>       // std::tuple, std::make_tuple

namespace hdmi {

// [C++] Un namespace anonyme rend ces fonctions invisibles depuis les autres
// .cpp : c'est l'équivalent d'une méthode « private static » en Java.
namespace {

// Rang de préférence d'un format : plus c'est grand, mieux c'est.
int formatRank(PixelFormat format) {
    switch (format) {
        case PixelFormat::Nv12:  return 3;  // natif GPU, aucune conversion côté CPU
        case PixelFormat::Yuy2:  return 2;  // non compressé, net
        case PixelFormat::Mjpeg: return 1;  // compressé : décodage + artefacts
        case PixelFormat::Other: break;
    }
    return 0;
}

// Fréquence plafonnée à 60 i/s et arrondie, pour comparer 59,94 et 60 comme
// égales et ne pas préférer un hypothétique 120 i/s au détriment du format.
long cappedFps(const VideoMode& mode) {
    return std::lround(std::min(mode.fps(), 60.0));
}

// Clé de tri d'un mode : on compare des tuples « lexicographiquement »,
// c'est-à-dire critère par critère, dans l'ordre (le premier qui diffère gagne).
//
// [C++ vs Java] std::tuple fournit déjà l'opérateur <. En Java on écrirait un
// Comparator.comparing(...).thenComparing(...) ; ici, une ligne suffit.
//
// [C++] « auto » laisse le compilateur déduire le type de retour
// (ici std::tuple<bool, bool, uint64_t, bool, long, int>), comme « var » en Java.
auto preferenceKey(const VideoMode& mode, ScreenSize screen) {
    const bool exactMatch  = mode.width == screen.width && mode.height == screen.height;
    const bool fitsScreen  = mode.width <= screen.width && mode.height <= screen.height;
    // Même proportions que l'écran : produit en croix sur 64 bits (pas de flottants).
    const bool sameAspect  = static_cast<uint64_t>(mode.width) * screen.height ==
                             static_cast<uint64_t>(mode.height) * screen.width;
    return std::make_tuple(exactMatch,          // 1. pixel pour pixel avec l'écran
                           fitsScreen,          // 2. ne dépasse pas l'écran
                           mode.area(),         // 3. le plus de pixels possible
                           sameAspect,          // 4. pas de bandes noires
                           cappedFps(mode),     // 5. le plus fluide (jusqu'à 60)
                           formatRank(mode.format)); // 6. le format le plus propre
}

// Renvoie le meilleur des modes fournis (déjà filtrés).
std::optional<VideoMode> pickBest(const std::vector<VideoMode>& candidates, ScreenSize screen) {
    if (candidates.empty()) {
        return std::nullopt;
    }
    // [C++] std::max_element parcourt la plage [begin, end) et renvoie un
    // ITÉRATEUR (une sorte de pointeur) vers le plus grand élément selon le
    // lambda de comparaison « a < b ».
    // [C++ vs Java] Le lambda « [screen](...) { ... } » CAPTURE la variable
    // screen par copie. En Java, la capture est implicite (variables
    // « effectivement finales ») ; en C++ on la déclare entre crochets.
    auto best = std::max_element(candidates.begin(), candidates.end(),
        [screen](const VideoMode& a, const VideoMode& b) {
            return preferenceKey(a, screen) < preferenceKey(b, screen);
        });
    return *best;   // « * » déréférence l'itérateur : on obtient (une copie de) l'élément
}

// Ne garde que les modes dont le format est exploitable.
std::vector<VideoMode> usableModes(const std::vector<VideoMode>& modes) {
    std::vector<VideoMode> result;
    result.reserve(modes.size());           // évite des réallocations successives
    for (const VideoMode& mode : modes) {   // boucle « for-each », comme en Java
        if (mode.format != PixelFormat::Other && mode.width > 0 && mode.height > 0) {
            result.push_back(mode);         // push_back ~ add, et COPIE l'élément
        }
    }
    return result;  // [C++] pas de copie ici : le vecteur est « déplacé » (move), voir doc §5
}

wchar_t lower(wchar_t c) {
    return static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
}

} // namespace (anonyme)

std::wstring describe(const VideoMode& mode) {
    return std::format(L"{}x{} @ {:.2f} i/s {}", mode.width, mode.height, mode.fps(),
                       toString(mode.format));
}

std::optional<VideoMode> selectBestMode(const std::vector<VideoMode>& modes, ScreenSize screen) {
    return pickBest(usableModes(modes), screen);
}

std::optional<VideoMode> findMode(const std::vector<VideoMode>& modes, uint32_t width,
                                  uint32_t height, uint32_t fps, ScreenSize screen) {
    std::vector<VideoMode> matching;
    for (const VideoMode& mode : usableModes(modes)) {
        const bool sizeOk = mode.width == width && mode.height == height;
        const bool fpsOk  = fps == 0 || std::lround(mode.fps()) == static_cast<long>(fps);
        if (sizeOk && fpsOk) {
            matching.push_back(mode);
        }
    }
    return pickBest(matching, screen);
}

int scoreCaptureDevice(const std::wstring& name, const std::vector<VideoMode>& modes) {
    int score = 0;

    // [C++] std::any_of = Stream.anyMatch en Java.
    const bool hasFullHd60 = std::any_of(modes.begin(), modes.end(), [](const VideoMode& m) {
        return m.width >= 1920 && m.height >= 1080 && m.fps() >= 50.0;
    });
    if (hasFullHd60) {
        score += 2;
    }

    // Mots que l'on trouve dans le nom des caméras intégrées.
    // [C++] Tableau C de pointeurs vers des chaînes constantes : léger, et sa
    // taille est connue à la compilation (boucle for-each possible).
    const wchar_t* webcamWords[] = {L"integrated", L"intégrée", L"webcam", L"front",
                                    L"facetime", L"ir camera", L"infrared"};
    const bool looksLikeWebcam = std::any_of(std::begin(webcamWords), std::end(webcamWords),
        [&name](const wchar_t* word) { return containsIgnoreCase(name, word); });
    if (!looksLikeWebcam) {
        score += 1;
    }
    return score;
}

bool containsIgnoreCase(const std::wstring& text, const std::wstring& fragment) {
    if (fragment.empty()) {
        return true;
    }
    // std::search avec un prédicat d'égalité insensible à la casse.
    auto it = std::search(text.begin(), text.end(), fragment.begin(), fragment.end(),
                          [](wchar_t a, wchar_t b) { return lower(a) == lower(b); });
    return it != text.end();
}

} // namespace hdmi
