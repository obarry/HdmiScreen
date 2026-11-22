// =============================================================================
// CommandLine.h — Options de la ligne de commande
// =============================================================================
//
// Logique pure (sans Windows), testée dans tests/SelectionTests.cpp.
// La liste des options et leur effet sont décrits dans le README.

#pragma once

#include "Geometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hdmi {

// Mode imposé par --mode LARGEURxHAUTEUR[@FPS].
struct RequestedMode {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;   // 0 = n'importe laquelle
};

// Toutes les options, avec leurs valeurs par défaut.
struct Options {
    bool showHelp = false;           // --help
    bool listDevices = false;        // --list
    bool windowed = false;           // --windowed : démarrer en fenêtre plutôt qu'en plein écran
    bool fullRange = false;          // --full-range : le boîtier envoie du 0-255 au lieu de 16-235
    std::wstring deviceFilter;       // --device "texte" : partie du nom du périphérique
    std::optional<RequestedMode> mode;   // --mode 1920x1200@60
    ScalingMode scaling = ScalingMode::Fit;  // --scaling fit|1to1|stretch
};

// Résultat de l'analyse : soit des options, soit un message d'erreur.
// [C++ vs Java] Pas d'exception ici : une erreur de saisie est un cas prévu,
// on la renvoie comme une valeur (comme un Either / Result).
struct ParseResult {
    Options options;
    std::wstring error;   // vide si tout va bien
    bool ok() const { return error.empty(); }
};

// Analyse les arguments, SANS le nom du programme (argv[1..]).
ParseResult parseArguments(const std::vector<std::wstring>& args);

// Analyse « 1920x1200 » ou « 1920x1200@60 ». Vide si le texte est invalide.
std::optional<RequestedMode> parseModeText(const std::wstring& text);

// Texte d'aide affiché par --help.
const wchar_t* helpText();

} // namespace hdmi
