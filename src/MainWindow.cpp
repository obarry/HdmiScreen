// =============================================================================
// MainWindow.cpp — La fenêtre Win32 (plein écran sans bordure)
// =============================================================================

#include "MainWindow.h"
#include "Commands.h"
#include "Log.h"

namespace hdmi {

namespace {

constexpr wchar_t kWindowClassName[] = L"HdmiScreenWindow";
constexpr UINT_PTR kCursorTimerId = 1;
constexpr UINT kCursorHideDelayMs = 2000;   // masquer le curseur après 2 s sans mouvement

// Rectangle de l'écran qui contient (le plus) la fenêtre.
RECT monitorRect(HWND hwnd) {
    // [API] MONITOR_DEFAULTTOPRIMARY : si la fenêtre n'est sur aucun écran, prendre le principal.
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);   // [API] convention Win32 : on indique la taille de la structure
    GetMonitorInfoW(monitor, &info);
    return info.rcMonitor;
}

} // namespace

MainWindow::MainWindow(HINSTANCE instance, bool startFullscreen) {
    // 1. Déclarer une « classe de fenêtre » : un modèle (procédure, curseur, fond).
    //    Rien à voir avec une classe C++ : c'est une notion Win32.
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &MainWindow::windowProc;   // pointeur vers notre fonction
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        throw HrError(HRESULT_FROM_WIN32(GetLastError()), "RegisterClassExW");
    }

    // 2. Créer la fenêtre, d'abord en mode fenêtré (1280×800).
    //    Le dernier paramètre (« this ») sera retransmis dans WM_NCCREATE :
    //    c'est ainsi qu'on relie la fenêtre Windows à notre objet C++.
    hwnd_ = CreateWindowExW(0, kWindowClassName, L"HdmiScreen", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 1280, 800, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        throw HrError(HRESULT_FROM_WIN32(GetLastError()), "CreateWindowExW");
    }
    GetWindowRect(hwnd_, &windowedRect_);

    ShowWindow(hwnd_, SW_SHOW);
    if (startFullscreen) {
        setFullscreen(true);
    }
    scheduleCursorHide();
}

MainWindow::~MainWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
    // La barre de menus est détachée de la fenêtre dans WM_DESTROY : elle n'a
    // donc pas été détruite avec elle, c'est à nous de le faire.
    if (menuBar_ != nullptr) {
        DestroyMenu(menuBar_);
    }
    UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
}

void MainWindow::setMenuBar(HMENU menu) {
    HMENU old = menuBar_;
    menuBar_ = menu;
    if (!fullscreen_) {
        // [API] SetMenu attache le menu à la fenêtre (ou le retire avec nullptr).
        // La zone client rétrécit de la hauteur de la barre : WM_SIZE suit.
        SetMenu(hwnd_, menuBar_);
    }
    if (old != nullptr) {
        DestroyMenu(old);   // détruit aussi tous ses sous-menus
    }
}

void MainWindow::setTitle(const std::wstring& title) {
    SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::clientSize(uint32_t& width, uint32_t& height) const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    width = static_cast<uint32_t>(rect.right - rect.left);
    height = static_cast<uint32_t>(rect.bottom - rect.top);
}

void MainWindow::setFullscreen(bool fullscreen) {
    if (fullscreen == fullscreen_) {
        return;
    }
    if (fullscreen) {
        GetWindowRect(hwnd_, &windowedRect_);   // mémoriser pour le retour
        const RECT screen = monitorRect(hwnd_);
        // [API] WS_POPUP : fenêtre sans bordure ni barre de titre. Couvrant
        // exactement l'écran, elle permet à Windows d'afficher la swap chain
        // sans composition supplémentaire (« independent flip »).
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetMenu(hwnd_, nullptr);   // pas de barre de menus en plein écran : clic droit
        SetWindowPos(hwnd_, HWND_TOP, screen.left, screen.top, screen.right - screen.left,
                     screen.bottom - screen.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetMenu(hwnd_, menuBar_);
        SetWindowPos(hwnd_, nullptr, windowedRect_.left, windowedRect_.top,
                     windowedRect_.right - windowedRect_.left, windowedRect_.bottom - windowedRect_.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER);
    }
    fullscreen_ = fullscreen;
}

void MainWindow::showCursor() {
    if (cursorHidden_) {
        cursorHidden_ = false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}

bool MainWindow::cursorInClientArea() const {
    POINT cursor{};
    GetCursorPos(&cursor);                 // position en coordonnées écran
    ScreenToClient(hwnd_, &cursor);        // -> coordonnées de la zone client
    RECT client{};
    GetClientRect(hwnd_, &client);
    return PtInRect(&client, cursor) != FALSE;
}

void MainWindow::showContextMenu(LPARAM lParam) {
    if (!buildContextMenu) {
        return;
    }
    HMENU menu = buildContextMenu();
    if (menu == nullptr) {
        return;
    }
    // [API] Position du clic dans lParam ; (-1, -1) si le menu a été demandé au
    // clavier (touche Menu ou Maj+F10) : on l'ouvre alors au centre.
    POINT at{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
    if (at.x == -1 && at.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        at = {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    }
    showCursor();
    inMenuLoop_ = true;   // le curseur reste visible tant que le menu est ouvert
    // [API] TPM_RETURNCMD : au lieu d'envoyer WM_COMMAND, TrackPopupMenu
    // renvoie directement l'identifiant choisi (0 si le menu est fermé sans choix).
    const UINT chosen = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, at.x, at.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    inMenuLoop_ = false;
    scheduleCursorHide();
    if (chosen != 0 && onCommand) {
        onCommand(chosen);
    }
}

void MainWindow::scheduleCursorHide() {
    // [API] SetTimer avec le même identifiant redémarre le minuteur : WM_TIMER
    // arrivera 2 s après le DERNIER mouvement de souris.
    SetTimer(hwnd_, kCursorTimerId, kCursorHideDelayMs, nullptr);
}

// -----------------------------------------------------------------------------
// Procédure de fenêtre
// -----------------------------------------------------------------------------

LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // [API] Astuce classique pour retrouver l'objet C++ :
    //  - au tout premier message utile (WM_NCCREATE), lParam contient le
    //    « this » passé à CreateWindowExW ; on le range dans la fenêtre
    //    (GWLP_USERDATA, un emplacement libre prévu pour ça) ;
    //  - pour les messages suivants, on le relit.
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    // [C++] reinterpret_cast : réinterprète des bits (ici un entier en pointeur).
    // C'est le cast le plus dangereux ; il n'a pas d'équivalent en Java.

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY) {
        // Tout dernier message de la fenêtre : on défait le lien avec l'objet C++.
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    // [C++] Barrière à exceptions : rien ne doit remonter vers Windows (code C).
    try {
        return self->handleMessage(message, wParam, lParam);
    } catch (const std::exception& e) {
        // [C++] catch par référence constante : évite une copie et le « slicing ».
        log::error(L"Exception dans la procédure de fenêtre");
        OutputDebugStringA(e.what());
        PostQuitMessage(1);
        return 0;
    }
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            // [API] LOWORD/HIWORD : la largeur et la hauteur sont empaquetées dans lParam.
            if (onResize) {   // un std::function vide se teste comme un booléen
                onResize(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;

        case WM_KEYDOWN: {
            // Les touches sont traduites en commandes, traitées par App comme
            // celles des menus : une seule logique pour les deux.
            uint32_t command = 0;
            switch (wParam) {   // wParam = code de touche virtuelle (VK_*)
                case VK_ESCAPE: command = command::Quit; break;
                case VK_F11:    command = command::ToggleFullscreen; break;
                case 'S':       command = command::ScalingCycle; break;   // code ASCII majuscule
                case 'R':       command = command::RangeToggle; break;
                default:        break;
            }
            if (command != 0 && onCommand) {
                onCommand(command);
                return 0;
            }
            break;
        }   // [C++] accolades obligatoires : un « case » qui déclare une variable

        case WM_COMMAND:
            // [API] Choix dans la barre de menus : identifiant dans le mot bas de wParam.
            if (HIWORD(wParam) == 0 && onCommand) {   // 0 = vient d'un menu
                onCommand(LOWORD(wParam));
                return 0;
            }
            break;

        case WM_CONTEXTMENU:
            showContextMenu(lParam);
            return 0;

        case WM_ENTERMENULOOP:
            inMenuLoop_ = true;
            showCursor();
            return 0;

        case WM_EXITMENULOOP:
            inMenuLoop_ = false;
            scheduleCursorHide();
            return 0;

        case WM_MOUSEMOVE:
            showCursor();
            scheduleCursorHide();
            return 0;

        case WM_TIMER:
            if (wParam == kCursorTimerId) {
                KillTimer(hwnd_, kCursorTimerId);
                // Ne pas masquer le curseur pendant qu'un menu est ouvert, ni
                // quand il est hors de l'image (barre de titre, menus, autre fenêtre).
                if (!inMenuLoop_ && cursorInClientArea()) {
                    cursorHidden_ = true;
                    SetCursor(nullptr);
                }
            }
            return 0;

        case WM_SETCURSOR:
            // Windows redemande le curseur à chaque mouvement : on répond
            // « aucun » tant qu'il doit rester masqué (zone client seulement).
            if (cursorHidden_ && LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;

        case WM_ERASEBKGND:
            return 1;   // D3D dessine tout : inutile d'effacer (évite un clignotement)

        case WM_DESTROY:
            // Détacher la barre de menus : sinon Windows la détruirait avec la
            // fenêtre, et notre destructeur la détruirait une seconde fois.
            SetMenu(hwnd_, nullptr);
            PostQuitMessage(0);   // fera sortir la boucle de messages (WM_QUIT)
            return 0;

        default:
            break;
    }
    // Comportement par défaut de Windows pour tout le reste (déplacement, Alt+F4…).
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace hdmi
