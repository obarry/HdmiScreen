// =============================================================================
// MainWindow.h — La fenêtre Win32 (plein écran sans bordure)
// =============================================================================
//
// Notions Win32 (classe de fenêtre, messages, procédure de fenêtre) :
// docs/API_WINDOWS.md §1. Le passage d'une fonction C à une méthode C++
// (windowProc -> handleMessage) est expliqué dans docs/CPP_POUR_DEV_JAVA.md §10.

#pragma once

#include "ComUtils.h"

#include <cstdint>
#include <functional>   // std::function ~ une interface fonctionnelle Java
#include <string>

namespace hdmi {

class MainWindow {
public:
    // Actions déclenchées par la fenêtre, branchées par App.
    // [C++ vs Java] std::function<void(uint32_t, uint32_t)> ~ BiConsumer<Integer, Integer> :
    // peut contenir un lambda, une fonction libre ou un objet appelable.
    std::function<void(uint32_t width, uint32_t height)> onResize;

    // Une commande (voir Commands.h) issue d'un menu ou d'un raccourci clavier.
    std::function<void(uint32_t command)> onCommand;

    // Construit le menu contextuel (clic droit), juste avant de l'afficher :
    // il reflète ainsi toujours l'état courant. La fenêtre le détruit après usage.
    std::function<HMENU()> buildContextMenu;

    MainWindow(HINSTANCE instance, bool startFullscreen);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    HWND handle() const { return hwnd_; }

    // Taille actuelle de la zone de dessin, en pixels physiques.
    void clientSize(uint32_t& width, uint32_t& height) const;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return fullscreen_; }

    // Remplace la barre de menus (visible seulement en mode fenêtré). La fenêtre
    // devient propriétaire du menu et détruit l'ancien.
    void setMenuBar(HMENU menu);

    void setTitle(const std::wstring& title);

private:
    // Procédure de fenêtre : fonction C appelée par Windows pour chaque message.
    // [C++] Elle doit être « static » (pas de this) : Windows ne connaît pas nos objets.
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // Version « méthode » vers laquelle windowProc redirige, avec accès aux membres.
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void showCursor();
    void scheduleCursorHide();
    bool cursorInClientArea() const;
    void showContextMenu(LPARAM lParam);

    HWND hwnd_ = nullptr;
    bool fullscreen_ = false;
    bool cursorHidden_ = false;
    bool inMenuLoop_ = false;      // un menu est ouvert : ne pas masquer le curseur
    HMENU menuBar_ = nullptr;      // barre de menus (possédée), retirée en plein écran
    RECT windowedRect_{};          // position à restaurer en quittant le plein écran
};

} // namespace hdmi
