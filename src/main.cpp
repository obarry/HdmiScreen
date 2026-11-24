// =============================================================================
// main.cpp — Point d'entrée de HdmiScreen
// =============================================================================
//
// [C++ vs Java] Pas de « public static void main » dans une classe : le point
// d'entrée est une fonction libre. Pour une application fenêtrée Windows, elle
// s'appelle wWinMain (le « w » = version Unicode / UTF-16).

#include "App.h"
#include "CaptureDevices.h"
#include "CommandLine.h"
#include "ComUtils.h"
#include "Log.h"
#include "SettingsFile.h"

#include <shellapi.h>   // [API] CommandLineToArgvW

#include <vector>

namespace {

// Découpe la ligne de commande en arguments, sans le nom du programme.
std::vector<std::wstring> commandLineArguments() {
    int count = 0;
    // [API] CommandLineToArgvW gère les guillemets comme le ferait argv en C.
    // Le tableau renvoyé doit être libéré avec LocalFree.
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> args;
    for (int i = 1; i < count; ++i) {
        args.emplace_back(argv[i]);   // emplace_back : construit l'élément sur place
    }
    LocalFree(argv);
    return args;
}

// Affiche un message : dans la console si on en a une, sinon dans une boîte.
void showMessage(const std::wstring& text, UINT icon) {
    if (hdmi::log::hasConsole()) {
        hdmi::log::print(text + L"\n");
    } else {
        MessageBoxW(nullptr, text.c_str(), L"HdmiScreen", MB_OK | icon);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previous*/, PWSTR /*commandLine*/, int /*show*/) {
    // [API] Indispensable sur un écran 2880×1800 à 200 % : sans cela, Windows
    // ferait croire à l'appli que l'écran mesure 1440×900, puis agrandirait
    // l'image lui-même (flou). Ici, on travaille en pixels physiques.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Réglages : valeurs par défaut < hdmiscreen.ini < ligne de commande.
    const std::wstring settingsPath = hdmi::defaultSettingsPath();
    const hdmi::ParseResult parsed =
        hdmi::parseArguments(commandLineArguments(), hdmi::loadSettings(settingsPath));
    const hdmi::Options& options = parsed.options;
    if (!parsed.ok() || options.showHelp || options.listDevices) {
        hdmi::log::attachParentConsole();   // sorties texte : vers l'invite si possible
    }
    if (!parsed.ok()) {
        showMessage(parsed.error + L"\n\n" + hdmi::helpText(), MB_ICONERROR);
        return 2;
    }
    if (options.showHelp) {
        showMessage(hdmi::helpText(), MB_ICONINFORMATION);
        return 0;
    }

    // [C++] Barrière finale : toute exception non traitée finit ici, avec un
    // message lisible, au lieu d'un plantage silencieux.
    try {
        // RAII : ces deux objets initialisent COM et Media Foundation, et les
        // arrêteront dans l'ordre inverse à la sortie du bloc, quoi qu'il arrive.
        const hdmi::ComInitializer com;
        const hdmi::MediaFoundationInitializer mediaFoundation;

        if (options.listDevices) {
            return hdmi::runListCommand(options);
        }
        hdmi::App app(instance, options, settingsPath);
        return app.run();
    } catch (const std::exception& e) {
        const std::wstring message = hdmi::widen(e.what());
        hdmi::log::error(message);
        showMessage(L"Erreur : " + message, MB_ICONERROR);
        return 1;
    }
}
