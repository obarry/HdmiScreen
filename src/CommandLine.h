// =============================================================================
// CommandLine.h — Options : ligne de commande et conversions texte
// =============================================================================
//
// Logique pure (sans Windows), testée dans tests/SelectionTests.cpp.
// La liste des options et leur effet sont décrits dans le README.
//
// Les mêmes options sont enregistrées dans hdmiscreen.ini (SettingsFile.cpp) :
// les fonctions de conversion texte ci-dessous servent aux deux.

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
// Ordre de priorité au lancement : valeurs par défaut < hdmiscreen.ini < ligne de commande.
struct Options {
    bool showHelp = false;           // --help
    bool listDevices = false;        // --list
    bool windowed = false;           // --windowed / --fullscreen : démarrer en fenêtre ou en plein écran
    bool fullRange = false;          // --full-range : le boîtier envoie du 0-255 au lieu de 16-235
    std::wstring deviceFilter;       // --device "texte" : partie du nom du périphérique
    std::optional<RequestedMode> mode;   // --mode 1920x1200@60
    ScalingMode scaling = ScalingMode::Fit;  // --scaling fit|1to1|stretch
    // Lissage du GPU par défaut : le plus éprouvé. Les filtres par shader n'apportent
    // de gain visible que si le signal reçu est déjà dégradé.
    UpscaleFilter upscale = UpscaleFilter::VideoProcessor;   // --upscale gpu|bicubic|lanczos
    int sharpness = 1;               // --sharpness 0..3 (0 = aucun renforcement)
};

// Résultat de l'analyse : soit des options, soit un message d'erreur.
// [C++ vs Java] Pas d'exception ici : une erreur de saisie est un cas prévu,
// on la renvoie comme une valeur (comme un Either / Result).
struct ParseResult {
    Options options;
    std::wstring error;   // vide si tout va bien
    bool ok() const { return error.empty(); }
};

// Analyse les arguments, SANS le nom du programme (argv[1..]), en partant des
// options « base » (celles lues dans hdmiscreen.ini) : un argument présent
// remplace la valeur correspondante, les autres valeurs sont conservées.
//
// [C++ vs Java] « = {} » est un ARGUMENT PAR DÉFAUT : on peut appeler
// parseArguments(args) ou parseArguments(args, base). Java n'en a pas ; on y
// écrirait deux méthodes surchargées.
ParseResult parseArguments(const std::vector<std::wstring>& args, const Options& base = {});

// Analyse « 1920x1200 » ou « 1920x1200@60 ». Vide si le texte est invalide.
std::optional<RequestedMode> parseModeText(const std::wstring& text);

// Inverse de parseModeText : « 1920x1200 » ou « 1920x1200@60 ».
std::wstring toText(const RequestedMode& mode);

// « fit », « 1to1 », « stretch » <-> ScalingMode.
// [C++] Deux fonctions toText de même nom, distinguées par le type de leur
// paramètre : c'est la surcharge (overloading), comme en Java.
const wchar_t* toText(ScalingMode scaling);
std::optional<ScalingMode> parseScalingText(const std::wstring& text);

// « gpu », « bicubic », « lanczos » <-> UpscaleFilter.
const wchar_t* toText(UpscaleFilter filter);
std::optional<UpscaleFilter> parseUpscaleText(const std::wstring& text);

// « 0 » à « 3 » -> niveau de netteté ; vide si hors limites.
std::optional<int> parseSharpnessText(const std::wstring& text);

// Texte d'aide affiché par --help.
const wchar_t* helpText();

} // namespace hdmi
