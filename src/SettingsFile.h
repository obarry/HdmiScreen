// =============================================================================
// SettingsFile.h — Réglages persistants dans hdmiscreen.ini
// =============================================================================
//
// Les choix faits dans les menus sont enregistrés dans un fichier texte
// hdmiscreen.ini, placé à côté de l'exécutable, et relus au lancement suivant.
//
// Exemple de contenu :
//   [capture]
//   device=
//   mode=auto
//   [display]
//   fullscreen=1
//   scaling=fit
//   colorRange=limited
//
// [API] Lecture/écriture avec GetPrivateProfileStringW et
// WritePrivateProfileStringW, les fonctions Win32 historiques pour les
// fichiers .ini. Voir docs/API_WINDOWS.md §1.7.

#pragma once

#include "CommandLine.h"

#include <string>

namespace hdmi {

// Chemin du fichier : même dossier et même nom que l'exécutable, extension .ini.
std::wstring defaultSettingsPath();

// Lit les réglages ; un fichier absent ou une valeur illisible donne la valeur par défaut.
Options loadSettings(const std::wstring& path);

// Enregistre les réglages persistants (périphérique, mode, affichage).
// Les options ponctuelles (--list, --help) ne sont pas enregistrées.
void saveSettings(const std::wstring& path, const Options& options);

} // namespace hdmi
