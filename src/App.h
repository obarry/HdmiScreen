// =============================================================================
// App.h — Assemblage de l'application et boucle principale
// =============================================================================
//
// App relie les briques : fenêtre (MainWindow), rendu (Renderer) et capture
// (CaptureSession). Le déroulement d'une image et la gestion des débranchements
// sont décrits dans docs/ARCHITECTURE.md.

#pragma once

#include "CaptureSession.h"
#include "CommandLine.h"
#include "ComUtils.h"
#include "MainWindow.h"
#include "Renderer.h"
#include "Selection.h"

#include <chrono>    // std::chrono::steady_clock ~ System.nanoTime
#include <memory>    // std::unique_ptr
#include <set>       // std::set ~ TreeSet
#include <string>

namespace hdmi {

// Taille de l'écran principal, en pixels physiques.
ScreenSize primaryScreenSize();

// Exécute « --list » : affiche les périphériques, leurs modes et les choix
// automatiques. Renvoie le code de sortie du programme.
int runListCommand(const Options& options);

class App {
public:
    App(HINSTANCE instance, const Options& options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Boucle principale ; rend la main quand la fenêtre est fermée.
    // Renvoie le code de sortie du programme.
    int run();

private:
    bool tryOpenCapture();
    void closeCapture();
    void renderPendingFrame();

    // [C++] Ordre des membres = ordre de construction ; la destruction se fait
    // dans l'ordre INVERSE. La session est donc détruite avant le rendu, et le
    // rendu avant la fenêtre : chacun survit à ce qui dépend de lui.
    Options options_;
    MainWindow window_;
    UniqueHandle frameEvent_;            // signalé par la capture à chaque image
    std::unique_ptr<Renderer> renderer_; // [C++] unique_ptr : seul propriétaire, delete automatique
    ComPtr<CaptureSession> session_;     // vide tant qu'aucun boîtier n'est ouvert
    ScreenSize screen_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastFrameTime_{};
    Clock::time_point lastOpenAttempt_{};
    bool showingNoSignal_ = false;

    // Tolérance aux coupures.
    std::wstring preferredLink_;           // boîtier utilisé en dernier (lien symbolique)
    std::set<std::wstring> ignoredLinks_;  // périphériques écartés (webcams) : plus rouverts
    bool waitingLogged_ = false;
    bool notReadyLogged_ = false;          // « détecté mais pas encore prêt » déjà signalé
    int disconnectCount_ = 0;              // nombre de coupures depuis le lancement
    Clock::time_point lostTime_{};         // instant de la dernière coupure
};

} // namespace hdmi
