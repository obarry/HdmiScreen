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

namespace hdmi {

class MainWindow {
public:
    // Actions déclenchées par la fenêtre, branchées par App.
    // [C++ vs Java] std::function<void(uint32_t, uint32_t)> ~ BiConsumer<Integer, Integer> :
    // peut contenir un lambda, une fonction libre ou un objet appelable.
    std::function<void(uint32_t width, uint32_t height)> onResize;
    std::function<void()> onToggleScaling;

    MainWindow(HINSTANCE instance, bool startFullscreen);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    HWND handle() const { return hwnd_; }

    // Taille actuelle de la zone de dessin, en pixels physiques.
    void clientSize(uint32_t& width, uint32_t& height) const;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return fullscreen_; }

private:
    // Procédure de fenêtre : fonction C appelée par Windows pour chaque message.
    // [C++] Elle doit être « static » (pas de this) : Windows ne connaît pas nos objets.
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // Version « méthode » vers laquelle windowProc redirige, avec accès aux membres.
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void showCursor();
    void scheduleCursorHide();

    HWND hwnd_ = nullptr;
    bool fullscreen_ = false;
    bool cursorHidden_ = false;
    RECT windowedRect_{};          // position à restaurer en quittant le plein écran
};

} // namespace hdmi
