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

// Vrai si « text » contient « fragment », sans tenir compte de la casse
// (utilisé pour l'option --device).
bool containsIgnoreCase(const std::wstring& text, const std::wstring& fragment);

} // namespace hdmi
