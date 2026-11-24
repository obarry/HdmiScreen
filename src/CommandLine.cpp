// =============================================================================
// CommandLine.cpp — Analyse des options de la ligne de commande
// =============================================================================

#include "CommandLine.h"

#include <cwchar>   // std::wcstoul

namespace hdmi {

namespace {

// Convertit un nombre décimal positif ; renvoie std::nullopt si invalide.
std::optional<uint32_t> parseNumber(const std::wstring& text) {
    if (text.empty() || text.size() > 6) {   // 6 chiffres suffisent largement
        return std::nullopt;
    }
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
    }
    // [C++] std::wcstoul : fonction héritée du C. c_str() donne un pointeur
    // vers les caractères terminés par un zéro, ce qu'attendent les API C.
    return static_cast<uint32_t>(std::wcstoul(text.c_str(), nullptr, 10));
}

} // namespace

std::optional<RequestedMode> parseModeText(const std::wstring& text) {
    // [C++] find renvoie std::wstring::npos (et non -1 comme indexOf en Java)
    // quand le caractère est absent.
    const auto x = text.find(L'x');
    if (x == std::wstring::npos) {
        return std::nullopt;
    }
    const auto at = text.find(L'@', x);
    const std::wstring widthText = text.substr(0, x);
    // substr(position, LONGUEUR) — attention : en Java, substring(début, FIN).
    const std::wstring heightText = text.substr(x + 1, at == std::wstring::npos ? std::wstring::npos
                                                                                 : at - x - 1);
    const std::wstring fpsText = at == std::wstring::npos ? L"" : text.substr(at + 1);

    const auto width = parseNumber(widthText);
    const auto height = parseNumber(heightText);
    // Sans « @ » : fréquence libre (0). Avec « @ » : un nombre est obligatoire.
    const auto fps = at == std::wstring::npos ? std::optional<uint32_t>(0) : parseNumber(fpsText);
    // [C++] Un std::optional se teste comme un booléen ; *width lit sa valeur.
    if (!width || !height || !fps || *width == 0 || *height == 0) {
        return std::nullopt;
    }
    return RequestedMode{*width, *height, *fps};
}

std::wstring toText(const RequestedMode& mode) {
    std::wstring text = std::to_wstring(mode.width) + L"x" + std::to_wstring(mode.height);
    if (mode.fps != 0) {
        text += L"@" + std::to_wstring(mode.fps);
    }
    return text;
}

const wchar_t* toText(ScalingMode scaling) {
    switch (scaling) {
        case ScalingMode::Fit:      return L"fit";
        case ScalingMode::OneToOne: return L"1to1";
        case ScalingMode::Stretch:  return L"stretch";
    }
    return L"fit";
}

std::optional<ScalingMode> parseScalingText(const std::wstring& text) {
    if (text == L"fit") return ScalingMode::Fit;
    if (text == L"1to1") return ScalingMode::OneToOne;
    if (text == L"stretch") return ScalingMode::Stretch;
    return std::nullopt;
}

ParseResult parseArguments(const std::vector<std::wstring>& args, const Options& base) {
    ParseResult result;
    result.options = base;               // copie des options de départ (struct = type valeur)
    Options& options = result.options;   // [C++] référence : un alias, pas une copie

    // [C++] size_t : entier non signé utilisé pour les tailles et les index.
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];

        // Lit la valeur qui suit une option (ex. --device "USB3 Video").
        // [C++] Lambda qui capture tout par référence ([&]) : elle peut
        // modifier i et result. Pratique, mais à manier avec soin.
        auto nextValue = [&](std::wstring& out) -> bool {
            if (i + 1 >= args.size()) {
                result.error = L"Valeur manquante après " + arg;
                return false;
            }
            out = args[++i];
            return true;
        };

        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {   // == compare le CONTENU
            options.showHelp = true;                               // (pas besoin de equals)
        } else if (arg == L"--list") {
            options.listDevices = true;
        } else if (arg == L"--windowed") {
            options.windowed = true;
        } else if (arg == L"--fullscreen") {
            options.windowed = false;
        } else if (arg == L"--full-range") {
            options.fullRange = true;
        } else if (arg == L"--device") {
            if (!nextValue(options.deviceFilter)) {
                return result;
            }
        } else if (arg == L"--mode") {
            std::wstring value;
            if (!nextValue(value)) {
                return result;
            }
            options.mode = parseModeText(value);
            if (!options.mode) {
                result.error = L"Mode invalide : " + value + L" (attendu : 1920x1200 ou 1920x1200@60)";
                return result;
            }
        } else if (arg == L"--scaling") {
            std::wstring value;
            if (!nextValue(value)) {
                return result;
            }
            const std::optional<ScalingMode> scaling = parseScalingText(value);
            if (!scaling) {
                result.error = L"Mise à l'échelle inconnue : " + value + L" (fit, 1to1 ou stretch)";
                return result;
            }
            options.scaling = *scaling;
        } else {
            result.error = L"Option inconnue : " + arg;
            return result;
        }
    }
    return result;
}

const wchar_t* helpText() {
    // [C++] Littéral « brut » R"(...)" : pas besoin d'échapper les retours à la
    // ligne ni les guillemets (comme les text blocks """ de Java 15+).
    return LR"(HdmiScreen - affiche en plein écran l'entrée d'un boîtier de capture HDMI

Usage : hdmiscreen.exe [options]

  --list                 liste les périphériques vidéo et leurs modes, puis quitte
  --device "texte"       utilise le périphérique dont le nom contient ce texte
  --mode 1920x1200[@60]  impose un mode de capture
  --scaling fit|1to1|stretch
                         mise à l'échelle (défaut : fit)
  --full-range           le signal est en plage complète 0-255 (défaut : 16-235)
  --windowed             démarre en fenêtre
  --fullscreen           démarre en plein écran
  --help                 affiche cette aide

Les réglages choisis dans les menus sont enregistrés dans hdmiscreen.ini, à côté
de l'exécutable ; les options ci-dessus les remplacent pour la session.

Raccourcis : Échap quitter, F11 plein écran/fenêtre, S mise à l'échelle,
R plage de couleur, clic droit menu.
)";
}

} // namespace hdmi
