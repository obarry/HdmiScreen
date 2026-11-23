// =============================================================================
// Selection.h — Choix automatique du périphérique et du mode de capture
// =============================================================================
//
// Logique PURE : aucune dépendance à Windows, donc testable en isolation
// (tests/SelectionTests.cpp). Les règles sont détaillées dans
// docs/ARCHITECTURE.md, section « Choix du mode ».

#pragma once

#include "VideoMode.h"

#include <optional>     // std::optional ~ java.util.Optional
#include <string>
#include <vector>       // std::vector ~ ArrayList

namespace hdmi {

// Taille de l'écran du portable perso, en pixels physiques.
struct ScreenSize {
    uint32_t width = 0;
    uint32_t height = 0;
};

// Choisit le meilleur mode de capture pour un écran donné.
// Renvoie std::nullopt (~ Optional.empty()) si aucun mode n'est exploitable.
//
// [C++ vs Java] « const std::vector<VideoMode>& » : on passe une RÉFÉRENCE
// constante. Sans le « & », le vecteur entier serait COPIÉ à chaque appel
// (en Java, les objets sont toujours passés par référence).
std::optional<VideoMode> selectBestMode(const std::vector<VideoMode>& modes,
                                        ScreenSize screen);

// Cherche un mode imposé par l'utilisateur (option --mode 1920x1200@60).
// fps == 0 signifie « n'importe quelle fréquence ». Parmi les modes qui
// correspondent, applique les mêmes préférences que selectBestMode.
std::optional<VideoMode> findMode(const std::vector<VideoMode>& modes,
                                  uint32_t width, uint32_t height, uint32_t fps,
                                  ScreenSize screen);

// Note la probabilité qu'un périphérique vidéo soit le boîtier de capture
// HDMI plutôt que la webcam intégrée du portable. Plus c'est haut, mieux c'est.
//  +2 : il propose au moins un mode >= 1920x1080 à >= 50 i/s (rare pour une webcam)
//  +1 : son nom ne contient pas de mot typique d'une webcam intégrée
int scoreCaptureDevice(const std::wstring& name, const std::vector<VideoMode>& modes);

// Score minimal pour qu'un périphérique soit retenu AUTOMATIQUEMENT (sans --device) :
// il doit proposer au moins un mode >= 1920x1080 à >= 50 i/s. Les webcams
// (généralement limitées à 30 i/s) sont ainsi écartées : mieux vaut l'écran
// « pas de signal » que la caméra du portable.
// [C++] constexpr sur une variable : constante connue à la compilation (~ static final).
constexpr int kMinimumAutoScore = 2;

// Ce que la sélection a besoin de savoir d'un périphérique.
struct DeviceCandidate {
    std::wstring symbolicLink;   // identifiant stable du périphérique
    int score = 0;               // résultat de scoreCaptureDevice
};

// Choisit le périphérique à utiliser parmi les candidats. Renvoie son index, ou -1.
//  1. Si le périphérique utilisé précédemment (preferredLink) est présent, on le
//     reprend, quel que soit son score : après une micro-coupure, on revient au même boîtier.
//  2. Sinon, le meilleur score (le premier en cas d'égalité), à condition
//     d'atteindre kMinimumAutoScore — sauf si l'utilisateur a désigné le
//     périphérique lui-même (userFiltered, option --device).
int chooseDevice(const std::vector<DeviceCandidate>& candidates, const std::wstring& preferredLink,
                 bool userFiltered);

// Vrai si « text » contient « fragment », sans tenir compte de la casse
// (utilisé pour l'option --device).
bool containsIgnoreCase(const std::wstring& text, const std::wstring& fragment);

} // namespace hdmi
