// =============================================================================
// Commands.h — Identifiants des commandes (menus et raccourcis clavier)
// =============================================================================
//
// Toute action de l'utilisateur, qu'elle vienne d'un menu, du menu contextuel
// ou d'une touche, devient un identifiant de commande traité en un seul
// endroit : App::onCommand. La fenêtre ne fait que traduire.
//
// [API] Win32 identifie chaque élément de menu par un entier (UINT), renvoyé
// dans le message WM_COMMAND quand on le choisit. Voir docs/API_WINDOWS.md §1.6.

#pragma once

#include <cstdint>

namespace hdmi {

// [C++] On utilise un enum « classique » (non « class ») dans un namespace
// dédié : ses valeurs se convertissent implicitement en entier, ce dont les
// API de menus Win32 ont besoin. Avec un enum class, il faudrait un
// static_cast à chaque appel.
namespace command {
enum : uint32_t {
    // Fichier
    Reconnect = 100,       // fermer et rouvrir la capture
    Quit,                  // Échap

    // Affichage
    ToggleFullscreen = 200, // F11
    ScalingFit,
    ScalingOneToOne,
    ScalingStretch,
    ScalingCycle,          // touche S : passe au mode suivant (pas dans les menus)
    RangeLimited,
    RangeFull,
    RangeToggle,           // touche R (pas dans les menus)
    UpscaleVideoProcessor,
    UpscaleBicubic,
    UpscaleLanczos,
    UpscaleCycle,          // touche F (pas dans les menus)
    Sharpness0,            // Sharpness0 + n = niveau n (0 à 3)
    Sharpness1,
    Sharpness2,
    Sharpness3,
    SharpnessCycle,        // touche N (pas dans les menus)

    // Aide
    ShowShortcuts = 300,
    About,

    // Périphérique et mode : listes construites à la volée.
    // Le périphérique n°i reçoit l'identifiant DeviceFirst + i, etc.
    DeviceAuto = 1000,
    DeviceFirst = 1001,    // jusqu'à DeviceFirst + 98
    ModeAuto = 2000,
    ModeFirst = 2001,      // jusqu'à ModeFirst + 98
    ListCapacity = 99
};
} // namespace command

} // namespace hdmi
