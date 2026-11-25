// =============================================================================
// App.cpp — Assemblage de l'application et boucle principale
// =============================================================================

#include "App.h"
#include "CaptureDevices.h"
#include "Commands.h"
#include "Log.h"
#include "SettingsFile.h"

#include <algorithm>
#include <format>

namespace hdmi {

namespace {

// [C++] « using namespace » limité à ce fichier : autorise 500ms, 3s…
using namespace std::chrono_literals;

// Après une perte du boîtier (micro-coupure USB), on retente vite : Windows met
// en général une à deux secondes à le redétecter.
constexpr auto kReconnectInterval = 500ms;  // fréquence des tentatives d'ouverture
// Pendant une courte coupure, on laisse la dernière image affichée plutôt que
// de faire clignoter l'écran gris.
constexpr auto kNoSignalDelay = 3s;         // délai sans image avant « pas de signal »
constexpr DWORD kLoopTimeoutMs = 250;       // réveil minimal de la boucle

// Un périphérique ouvert et inspecté, candidat à la capture.
struct ProbedDevice {
    CaptureDeviceInfo info;
    ComPtr<IMFMediaSource> source;
    std::vector<VideoMode> modes;
    int score = 0;
};

// Un périphérique listé par Windows mais impossible à ouvrir pour l'instant
// (occupé par une autre appli, ou boîtier pas encore prêt après une coupure).
struct ProbeFailure {
    std::wstring name;
    std::wstring message;
};

// Ouvre tous les périphériques (filtrés par --device) et lit leurs modes.
// Ceux dont le lien symbolique figure dans « skipped » ne sont pas rouverts
// (webcam déjà écartée : inutile de la réveiller toutes les 500 ms).
// Les sources non retenues devront être fermées par shutdownAll.
//
// [C++ vs Java] std::set ~ TreeSet (trié) ; std::unordered_set ~ HashSet.
// count(x) renvoie 0 ou 1 : c'est le « contains » des ensembles C++ (C++20 a aussi contains).
//
// Les échecs d'ouverture ne sont pas journalisés ici mais ajoutés à « failures » :
// c'est l'appelant qui décide s'ils méritent un message.
// [C++] Paramètre de sortie par référence non constante : la fonction remplit
// le vecteur de l'appelant (en Java, on passerait une List à remplir).
std::vector<ProbedDevice> probeDevices(const Options& options, const std::set<std::wstring>& skipped,
                                       std::vector<ProbeFailure>& failures) {
    std::vector<ProbedDevice> probed;
    for (const CaptureDeviceInfo& info : enumerateCaptureDevices()) {
        if (!options.deviceFilter.empty() && !containsIgnoreCase(info.name, options.deviceFilter)) {
            continue;
        }
        if (skipped.count(info.symbolicLink) != 0) {
            continue;
        }
        try {
            ProbedDevice device;
            device.info = info;
            device.source = openDevice(info);
            device.modes = listNativeModes(device.source.Get());
            device.score = scoreCaptureDevice(info.name, device.modes);
            // [C++] std::move : on « vide » device dans le vecteur au lieu de le
            // copier (pas de AddRef/Release inutile sur la source).
            probed.push_back(std::move(device));
        } catch (const HrError& e) {
            // Un périphérique indisponible ne doit pas empêcher d'inspecter les autres.
            failures.push_back({info.name, widen(e.what())});
        }
    }
    return probed;
}

void shutdownAll(std::vector<ProbedDevice>& devices) {
    for (ProbedDevice& device : devices) {
        if (device.source) {
            // [API] Une source ouverte mais jamais confiée à un lecteur doit
            // être arrêtée explicitement, sinon le périphérique reste réservé.
            device.source->Shutdown();
            device.source.Reset();
        }
    }
}

// Index du périphérique à utiliser, ou -1 (règles : chooseDevice, Selection.h).
int deviceIndex(const std::vector<ProbedDevice>& devices, const std::wstring& preferredLink,
                const Options& options) {
    std::vector<DeviceCandidate> candidates;
    for (const ProbedDevice& device : devices) {
        candidates.push_back({device.info.symbolicLink, device.score});
    }
    return chooseDevice(candidates, preferredLink, !options.deviceFilter.empty());
}

// Vrai si ce périphérique ne sera jamais retenu automatiquement (webcam…).
bool belowAutoThreshold(const ProbedDevice& device, const Options& options) {
    return options.deviceFilter.empty() && device.score < kMinimumAutoScore;
}

// Mode à utiliser pour un périphérique : celui imposé par --mode, sinon le meilleur.
std::optional<VideoMode> chooseMode(const Options& options, const std::vector<VideoMode>& modes,
                                    ScreenSize screen) {
    if (options.mode) {
        return findMode(modes, options.mode->width, options.mode->height, options.mode->fps, screen);
    }
    return selectBestMode(modes, screen);
}

// Garde l'écran allumé tant que l'objet existe (RAII).
class KeepDisplayOn {
public:
    KeepDisplayOn() { SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED); }
    ~KeepDisplayOn() { SetThreadExecutionState(ES_CONTINUOUS); }
    KeepDisplayOn(const KeepDisplayOn&) = delete;
    KeepDisplayOn& operator=(const KeepDisplayOn&) = delete;
};

} // namespace

ScreenSize primaryScreenSize() {
    // [API] Grâce au mode « DPI per monitor » activé dans main.cpp, ces valeurs
    // sont en pixels PHYSIQUES (2880×1800) et non mis à l'échelle (1440×900 à 200 %).
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return ScreenSize{static_cast<uint32_t>(info.rcMonitor.right - info.rcMonitor.left),
                      static_cast<uint32_t>(info.rcMonitor.bottom - info.rcMonitor.top)};
}

// -----------------------------------------------------------------------------
// --list
// -----------------------------------------------------------------------------

int runListCommand(const Options& options) {
    const ScreenSize screen = primaryScreenSize();
    std::vector<ProbeFailure> failures;
    std::vector<ProbedDevice> devices = probeDevices(options, {}, failures);
    const int best = deviceIndex(devices, L"", options);
    for (const ProbeFailure& failure : failures) {
        log::error(failure.name + L" : " + failure.message);
    }

    std::wstring text = std::format(L"Écran principal : {}x{}\n\n", screen.width, screen.height);
    if (devices.empty()) {
        text += L"Aucun périphérique de capture vidéo trouvé.\n";
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        const ProbedDevice& device = devices[i];
        const bool chosen = static_cast<int>(i) == best;
        const wchar_t* status = chosen ? L"  <= périphérique retenu"
                                : belowAutoThreshold(device, options)
                                    ? L"  (ignoré : pas de mode 1080p à 50 i/s ou plus, webcam probable)"
                                    : L"";
        text += std::format(L"[{}] {}  (score {}){}\n", i + 1, device.info.name, device.score, status);
        const std::optional<VideoMode> mode = chooseMode(options, device.modes, screen);
        for (const VideoMode& m : device.modes) {
            const bool isChosenMode = mode && mode->nativeIndex == m.nativeIndex;
            text += std::format(L"      {}{}\n", describe(m), isChosenMode ? L"  <= mode retenu" : L"");
        }
        text += L"\n";
    }
    shutdownAll(devices);

    if (log::hasConsole()) {
        log::print(text);
    } else {
        MessageBoxW(nullptr, text.c_str(), L"HdmiScreen — périphériques", MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// App
// -----------------------------------------------------------------------------

App::App(HINSTANCE instance, const Options& options, std::wstring settingsPath)
    // [C++] Liste d'initialisation : construit les membres AVANT le corps du
    // constructeur, dans l'ordre de leur déclaration dans App.h.
    : options_(options),
      window_(instance, !options.windowed),
      // Événement à réinitialisation AUTOMATIQUE : il repasse à « non signalé »
      // dès qu'une attente l'a consommé.
      frameEvent_(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
      renderer_(std::make_unique<Renderer>(window_.handle())),
      screen_(primaryScreenSize()),
      // [C++] settingsPath est reçu PAR VALEUR puis déplacé dans le membre :
      // une seule copie au total, que l'appelant passe un temporaire ou non.
      settingsPath_(std::move(settingsPath)) {
    if (!frameEvent_) {
        throw HrError(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }
    renderer_->setScalingMode(options.scaling);
    renderer_->setFullRange(options.fullRange);
    renderer_->setUpscaleFilter(options.upscale);
    renderer_->setSharpness(options.sharpness);

    // Brancher les actions de la fenêtre.
    // [C++] Le lambda capture « this » : il ne doit pas survivre à l'objet App
    // (garanti ici, la fenêtre étant un membre détruit avec App).
    window_.onResize = [this](uint32_t width, uint32_t height) { renderer_->resize(width, height); };
    window_.onCommand = [this](uint32_t command) { onCommand(command); };
    window_.buildContextMenu = [this]() {
        // Même instantané pour le menu contextuel et la barre de menus : les
        // identifiants DeviceFirst + i désignent le même périphérique dans les deux.
        snapshotMenuLists();
        refreshMenuBar();
        return buildContextMenu();
    };
    snapshotMenuLists();
    refreshMenuBar();
    updateTitle();

    // La fenêtre a pu changer de taille avant que onResize soit branché.
    uint32_t width = 0;
    uint32_t height = 0;
    window_.clientSize(width, height);
    renderer_->resize(width, height);
}

App::~App() {
    closeCapture();
}

bool App::tryOpenCapture() {
    lastOpenAttempt_ = Clock::now();
    std::vector<ProbeFailure> failures;
    std::vector<ProbedDevice> devices = probeDevices(options_, ignoredLinks_, failures);

    // Après une coupure, Windows réannonce le boîtier un peu avant qu'il soit
    // utilisable : l'ouverture échoue alors quelques fois de suite. On le
    // signale une seule fois par période d'attente, pas toutes les 500 ms.
    if (!failures.empty() && !notReadyLogged_) {
        for (const ProbeFailure& failure : failures) {
            log::info(failure.name + L" détecté mais pas encore prêt (" + failure.message +
                      L") : nouvelles tentatives en cours");
        }
        notReadyLogged_ = true;
    }

    // Mémoriser les périphériques qui ne seront jamais retenus, pour ne plus les rouvrir.
    for (const ProbedDevice& device : devices) {
        if (belowAutoThreshold(device, options_) && device.info.symbolicLink != preferredLink_) {
            if (ignoredLinks_.insert(device.info.symbolicLink).second) {   // second : vraiment ajouté
                log::info(device.info.name + L" ignoré : pas un boîtier de capture (webcam probable)");
            }
        }
    }

    const int best = deviceIndex(devices, preferredLink_, options_);
    if (best < 0) {
        // Message une seule fois par période d'attente, pas toutes les 500 ms.
        if (!waitingLogged_) {
            log::info(L"Aucun boîtier de capture : en attente de sa connexion");
            waitingLogged_ = true;
        }
        shutdownAll(devices);
        return false;
    }

    ProbedDevice& device = devices[static_cast<size_t>(best)];
    const std::optional<VideoMode> mode = chooseMode(options_, device.modes, screen_);
    if (!mode) {
        log::error(device.info.name + L" : aucun mode utilisable (voir --list)");
        shutdownAll(devices);
        return false;
    }

    try {
        session_ = CaptureSession::create(device.source.Get(), renderer_->deviceManager(), *mode,
                                          frameEvent_.get());
        // La source appartient désormais au lecteur de la session, qui l'arrêtera.
        device.source.Reset();
        session_->start();
    } catch (const HrError& e) {
        log::error(std::format(L"Ouverture de {} : {}", device.info.name, widen(e.what())));
        session_.Reset();
        shutdownAll(devices);
        return false;
    }
    shutdownAll(devices);   // ferme les autres périphériques inspectés

    const FrameFormat& out = session_->frameFormat();
    log::info(std::format(L"Capture : {} en {} ; images livrées en {}x{} {}", device.info.name,
                          describe(*mode), out.width, out.height, toString(out.format)));
    // Après une coupure : durée d'indisponibilité, utile pour juger le matériel.
    if (disconnectCount_ > 0 && lostTime_ != Clock::time_point{}) {
        // [C++] duration_cast convertit une durée dans une autre unité (ici en ms).
        const auto downtime = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lostTime_);
        log::info(std::format(L"Reprise après {:.1f} s d'interruption (coupure n° {})",
                              downtime.count() / 1000.0, disconnectCount_));
        lostTime_ = Clock::time_point{};
    }
    // Ce boîtier devient le périphérique « préféré » : après une coupure, c'est lui
    // qu'on attend et qu'on reprend.
    preferredLink_ = device.info.symbolicLink;
    waitingLogged_ = false;
    notReadyLogged_ = false;
    lastFrameTime_ = Clock::now();

    currentDeviceName_ = device.info.name;
    currentModes_ = device.modes;
    currentMode_ = mode;
    snapshotMenuLists();
    refreshMenuBar();
    updateTitle();
    return true;
}

void App::closeCapture() {
    if (session_) {
        session_->stop();
        session_.Reset();
        currentMode_.reset();
        updateTitle();
    }
}

// Ferme puis rouvre la capture, après un changement de périphérique ou de mode.
void App::restartCapture() {
    closeCapture();
    preferredLink_.clear();   // le choix de l'utilisateur prime sur le dernier boîtier
    ignoredLinks_.clear();    // réexaminer tous les périphériques (une webcam peut être choisie)
    waitingLogged_ = false;
    notReadyLogged_ = false;
    tryOpenCapture();
}

void App::renderPendingFrame() {
    if (!session_) {
        return;
    }
    ComPtr<IMFSample> sample = session_->takeLatestSample();
    if (!sample) {
        return;
    }
    // [API] Attendre que la swap chain soit prête (au plus 100 ms) : on dessine
    // ainsi toujours l'image la plus récente, sans file d'attente.
    WaitForSingleObjectEx(renderer_->frameLatencyWaitable(), 100, TRUE);
    renderer_->drawFrame(sample.Get(), session_->frameFormat());
    lastFrameTime_ = Clock::now();
    showingNoSignal_ = false;
}

int App::run() {
    const KeepDisplayOn keepDisplayOn;
    renderer_->drawNoSignal();
    showingNoSignal_ = true;
    tryOpenCapture();

    HANDLE frameEvent = frameEvent_.get();
    for (;;) {
        // [API] Attend SOIT une nouvelle image (frameEvent), SOIT un message
        // Windows (clavier, souris, redimensionnement…), SOIT 250 ms.
        // C'est le cœur d'une appli Win32 réactive sans consommer de CPU.
        const DWORD wait = MsgWaitForMultipleObjectsEx(1, &frameEvent, kLoopTimeoutMs, QS_ALLINPUT,
                                                       MWMO_INPUTAVAILABLE);

        // 1. Traiter tous les messages en attente.
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);   // transforme les touches en caractères (WM_CHAR)
            DispatchMessageW(&message);   // appelle MainWindow::windowProc
        }

        // 2. Afficher la nouvelle image s'il y en a une.
        if (wait == WAIT_OBJECT_0) {
            renderPendingFrame();
        }

        // 3. Capture en erreur (boîtier débranché…) : fermer, on retentera.
        if (session_ && FAILED(session_->error())) {
            ++disconnectCount_;
            lostTime_ = Clock::now();
            log::error(std::format(L"Capture interrompue, coupure n° {} (HRESULT 0x{:08X})",
                                   disconnectCount_, static_cast<unsigned long>(session_->error())));
            closeCapture();
        }

        // 4. Pas de session : retenter régulièrement l'ouverture.
        if (!session_ && Clock::now() - lastOpenAttempt_ >= kReconnectInterval) {
            tryOpenCapture();
        }

        // 5. Plus d'image depuis un moment : afficher « pas de signal ».
        if (!showingNoSignal_ && Clock::now() - lastFrameTime_ >= kNoSignalDelay) {
            renderer_->drawNoSignal();
            showingNoSignal_ = true;
        }
    }
}

// -----------------------------------------------------------------------------
// Menus et commandes
// -----------------------------------------------------------------------------
// [API] Les menus sont construits par programme (CreateMenu, AppendMenuW)
// plutôt que décrits dans un fichier de ressources .rc : leurs listes de
// périphériques et de modes changent selon le matériel branché. Chaque
// construction crée des menus neufs, qui reflètent l'état courant (cases cochées).
// Voir docs/API_WINDOWS.md §1.6.

namespace {

// Ajoute un élément de menu, éventuellement coché.
// [API] MF_STRING : élément texte ; MF_CHECKED : coche à gauche. Le texte après
// une tabulation (\t) s'affiche aligné à droite : on y indique le raccourci.
// « & » souligne la lettre d'accès clavier (Alt+lettre).
void addItem(HMENU menu, uint32_t id, const std::wstring& text, bool checked = false,
             bool enabled = true) {
    UINT flags = MF_STRING;
    if (checked) flags |= MF_CHECKED;
    if (!enabled) flags |= MF_GRAYED;
    AppendMenuW(menu, flags, id, text.c_str());
}

void addSeparator(HMENU menu) { AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); }

// Ajoute un sous-menu. Le menu parent en devient propriétaire : détruire le
// parent (DestroyMenu) détruit aussi ses sous-menus.
// [C++] reinterpret_cast : l'API attend le handle du sous-menu déguisé en
// entier (UINT_PTR), à la place de l'identifiant de commande.
void addSubmenu(HMENU menu, HMENU submenu, const wchar_t* text) {
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), text);
}

} // namespace

void App::snapshotMenuLists() {
    // Périphériques : simple énumération (rapide, sans ouvrir les périphériques).
    menuDevices_.clear();
    try {
        for (const CaptureDeviceInfo& info : enumerateCaptureDevices()) {
            if (menuDevices_.size() >= command::ListCapacity) {
                break;
            }
            menuDevices_.push_back({info.name, ignoredLinks_.count(info.symbolicLink) != 0});
        }
    } catch (const HrError& e) {
        log::error(L"Liste des périphériques : " + widen(e.what()));
    }

    // Modes : une entrée par taille distincte du boîtier en cours, la plus grande
    // en premier. La fréquence et le format restent choisis automatiquement.
    menuModes_.clear();
    for (const VideoMode& mode : currentModes_) {
        if (mode.format == PixelFormat::Other) {
            continue;
        }
        const RequestedMode size{mode.width, mode.height, 0};
        // [C++] std::find_if ~ stream().filter().findFirst() : renvoie un
        // itérateur sur le premier élément qui vérifie le prédicat, ou end().
        const auto existing = std::find_if(menuModes_.begin(), menuModes_.end(),
            [&size](const RequestedMode& m) { return m.width == size.width && m.height == size.height; });
        if (existing == menuModes_.end() && menuModes_.size() < command::ListCapacity) {
            menuModes_.push_back(size);
        }
    }
    std::sort(menuModes_.begin(), menuModes_.end(), [](const RequestedMode& a, const RequestedMode& b) {
        return static_cast<uint64_t>(a.width) * a.height > static_cast<uint64_t>(b.width) * b.height;
    });
}

HMENU App::buildFileMenu() const {
    HMENU menu = CreatePopupMenu();
    addItem(menu, command::Reconnect, L"&Reconnecter le boîtier");
    addSeparator(menu);
    addItem(menu, command::Quit, L"&Quitter\tÉchap");
    return menu;
}

HMENU App::buildDisplayMenu() const {
    const ScalingMode scaling = renderer_->scalingMode();
    HMENU menu = CreatePopupMenu();
    addItem(menu, command::ToggleFullscreen, L"&Plein écran\tF11", window_.isFullscreen());
    addSeparator(menu);
    addItem(menu, command::ScalingFit, L"&Ajuster (proportions conservées)\tS", scaling == ScalingMode::Fit);
    addItem(menu, command::ScalingOneToOne, L"Pixel pour pixel (&1:1)\tS", scaling == ScalingMode::OneToOne);
    addItem(menu, command::ScalingStretch, L"É&tirer (plein écran déformé)\tS", scaling == ScalingMode::Stretch);
    addSeparator(menu);
    addItem(menu, command::RangeLimited, L"Plage de couleur &limitée (16-235)\tR", !options_.fullRange);
    addItem(menu, command::RangeFull, L"Plage de couleur &complète (0-255)\tR", options_.fullRange);
    addSeparator(menu);

    // Filtre d'agrandissement : les filtres par shader sont grisés si le GPU
    // ne permet pas de les utiliser.
    const UpscaleFilter filter = renderer_->upscaleFilter();
    const bool shaders = renderer_->shadersAvailable();
    addItem(menu, command::UpscaleVideoProcessor, L"Agrandissement : lissage du &GPU (processeur vidéo)\tF",
            filter == UpscaleFilter::VideoProcessor);
    addItem(menu, command::UpscaleBicubic, L"Agrandissement : &bicubique net (Catmull-Rom)\tF",
            filter == UpscaleFilter::Bicubic, shaders);
    addItem(menu, command::UpscaleLanczos, L"Agrandissement : La&nczos (le plus net)\tF",
            filter == UpscaleFilter::Lanczos, shaders);

    // Netteté : seulement avec les filtres par shader.
    HMENU sharpness = CreatePopupMenu();
    const wchar_t* levels[] = {L"Aucune", L"Légère", L"Moyenne", L"Forte"};
    for (int level = 0; level <= kMaxSharpnessLevel; ++level) {
        addItem(sharpness, command::Sharpness0 + static_cast<uint32_t>(level),
                std::wstring(levels[level]) + L"\tN", renderer_->sharpness() == level);
    }
    const bool sharpnessUsable = shaders && filter != UpscaleFilter::VideoProcessor;
    AppendMenuW(menu, MF_POPUP | (sharpnessUsable ? 0u : static_cast<UINT>(MF_GRAYED)),
                reinterpret_cast<UINT_PTR>(sharpness), L"Ne&tteté");
    return menu;
}

HMENU App::buildDeviceMenu() const {
    HMENU menu = CreatePopupMenu();
    addItem(menu, command::DeviceAuto, L"&Automatique (boîtier de capture détecté)", options_.deviceFilter.empty());
    if (!menuDevices_.empty()) {
        addSeparator(menu);
    }
    for (size_t i = 0; i < menuDevices_.size(); ++i) {
        const MenuDevice& device = menuDevices_[i];
        std::wstring label = device.name;
        if (session_ && device.name == currentDeviceName_) {
            label += L"  (en cours)";
        } else if (device.ignored) {
            label += L"  (webcam, ignorée en automatique)";
        }
        addItem(menu, command::DeviceFirst + static_cast<uint32_t>(i), label,
                !options_.deviceFilter.empty() && device.name == options_.deviceFilter);
    }
    return menu;
}

HMENU App::buildModeMenu() const {
    HMENU menu = CreatePopupMenu();
    std::wstring autoLabel = L"&Automatique (meilleur mode pour l'écran)";
    if (!options_.mode && currentMode_) {
        autoLabel = L"&Automatique : " + describe(*currentMode_);
    }
    addItem(menu, command::ModeAuto, autoLabel, !options_.mode);
    if (menuModes_.empty()) {
        addItem(menu, 0, L"(modes connus une fois le boîtier ouvert)", false, false);
        return menu;
    }
    addSeparator(menu);
    for (size_t i = 0; i < menuModes_.size(); ++i) {
        const RequestedMode& m = menuModes_[i];
        const bool checked = options_.mode && options_.mode->width == m.width && options_.mode->height == m.height;
        addItem(menu, command::ModeFirst + static_cast<uint32_t>(i),
                std::format(L"{} × {}", m.width, m.height), checked);
    }
    return menu;
}

HMENU App::buildHelpMenu() const {
    HMENU menu = CreatePopupMenu();
    addItem(menu, command::ShowShortcuts, L"&Raccourcis clavier");
    addItem(menu, command::About, L"À &propos de HdmiScreen");
    return menu;
}

HMENU App::buildMenuBar() const {
    // [API] CreateMenu : une barre de menus ; CreatePopupMenu : un menu déroulant.
    HMENU bar = CreateMenu();
    addSubmenu(bar, buildFileMenu(), L"&Fichier");
    addSubmenu(bar, buildDisplayMenu(), L"&Affichage");
    addSubmenu(bar, buildDeviceMenu(), L"&Périphérique");
    addSubmenu(bar, buildModeMenu(), L"&Mode");
    addSubmenu(bar, buildHelpMenu(), L"Aid&e");
    return bar;
}

HMENU App::buildContextMenu() const {
    // Même contenu que la barre de menus, réorganisé pour un accès rapide.
    HMENU menu = CreatePopupMenu();
    addItem(menu, command::ToggleFullscreen, L"&Plein écran\tF11", window_.isFullscreen());
    addSeparator(menu);
    addSubmenu(menu, buildDisplayMenu(), L"&Affichage");
    addSubmenu(menu, buildDeviceMenu(), L"Pé&riphérique");
    addSubmenu(menu, buildModeMenu(), L"&Mode");
    addSeparator(menu);
    addItem(menu, command::Reconnect, L"Re&connecter le boîtier");
    addSubmenu(menu, buildHelpMenu(), L"Aid&e");
    addSeparator(menu);
    addItem(menu, command::Quit, L"&Quitter\tÉchap");
    return menu;
}

void App::refreshMenuBar() {
    window_.setMenuBar(buildMenuBar());
}

void App::updateTitle() {
    std::wstring title = L"HdmiScreen";
    if (session_ && currentMode_) {
        title += L" — " + currentDeviceName_ + L" · " + describe(*currentMode_);
        // Filtre d'agrandissement : utile pour comparer les réglages.
        const UpscaleFilter filter = renderer_->shadersAvailable() ? renderer_->upscaleFilter()
                                                                   : UpscaleFilter::VideoProcessor;
        switch (filter) {
            case UpscaleFilter::VideoProcessor: title += L" · lissage GPU"; break;
            case UpscaleFilter::Bicubic:        title += L" · bicubique"; break;
            case UpscaleFilter::Lanczos:        title += L" · Lanczos"; break;
        }
        if (filter != UpscaleFilter::VideoProcessor) {
            title += L", netteté " + std::to_wstring(renderer_->sharpness());
        }
    } else {
        title += L" — en attente du boîtier";
    }
    window_.setTitle(title);
}

void App::persistSettings() {
    saveSettings(settingsPath_, options_);
}

void App::onCommand(uint32_t id) {
    // Choix de la mise à l'échelle et de la plage, factorisés dans des lambdas locales.
    auto setScaling = [this](ScalingMode scaling) {
        renderer_->setScalingMode(scaling);
        options_.scaling = scaling;
    };
    auto setFullRange = [this](bool fullRange) {
        renderer_->setFullRange(fullRange);
        options_.fullRange = fullRange;
    };
    auto setUpscale = [this](UpscaleFilter filter) {
        renderer_->setUpscaleFilter(filter);
        options_.upscale = filter;
        log::info(std::wstring(L"Agrandissement : ") + toText(filter));
    };
    auto setSharpness = [this](int level) {
        renderer_->setSharpness(level);
        options_.sharpness = level;
        log::info(L"Netteté : " + std::to_wstring(level));
    };

    // [C++] Un switch ne fonctionne que sur des entiers ou des enums (pas sur des
    // chaînes comme en Java depuis la version 7).
    switch (id) {
        case command::Reconnect:
            log::info(L"Reconnexion demandée");
            closeCapture();
            ignoredLinks_.clear();
            tryOpenCapture();
            break;
        case command::Quit:
            DestroyWindow(window_.handle());
            return;   // la fenêtre n'existe plus : ne plus toucher aux menus
        case command::ToggleFullscreen:
            window_.setFullscreen(!window_.isFullscreen());
            options_.windowed = !window_.isFullscreen();
            break;
        case command::ScalingFit:       setScaling(ScalingMode::Fit); break;
        case command::ScalingOneToOne:  setScaling(ScalingMode::OneToOne); break;
        case command::ScalingStretch:   setScaling(ScalingMode::Stretch); break;
        case command::ScalingCycle:
            switch (renderer_->scalingMode()) {
                case ScalingMode::Fit:      setScaling(ScalingMode::OneToOne); break;
                case ScalingMode::OneToOne: setScaling(ScalingMode::Stretch); break;
                case ScalingMode::Stretch:  setScaling(ScalingMode::Fit); break;
            }
            break;
        case command::RangeLimited: setFullRange(false); break;
        case command::RangeFull:    setFullRange(true); break;
        case command::RangeToggle:  setFullRange(!options_.fullRange); break;
        case command::UpscaleVideoProcessor: setUpscale(UpscaleFilter::VideoProcessor); break;
        case command::UpscaleBicubic:        setUpscale(UpscaleFilter::Bicubic); break;
        case command::UpscaleLanczos:        setUpscale(UpscaleFilter::Lanczos); break;
        case command::UpscaleCycle:
            // Ordre : lissage GPU -> bicubique -> Lanczos -> lissage GPU…
            // Sans shaders, le cycle reste sur le lissage GPU.
            if (!renderer_->shadersAvailable()) {
                setUpscale(UpscaleFilter::VideoProcessor);
            } else if (options_.upscale == UpscaleFilter::VideoProcessor) {
                setUpscale(UpscaleFilter::Bicubic);
            } else if (options_.upscale == UpscaleFilter::Bicubic) {
                setUpscale(UpscaleFilter::Lanczos);
            } else {
                setUpscale(UpscaleFilter::VideoProcessor);
            }
            break;
        case command::Sharpness0:
        case command::Sharpness1:
        case command::Sharpness2:
        case command::Sharpness3:
            // [C++] Plusieurs « case » à la suite partagent le même code
            // (pas de « break » entre eux) : c'est le « fall-through », comme en Java.
            setSharpness(static_cast<int>(id - command::Sharpness0));
            break;
        case command::SharpnessCycle:
            setSharpness((options_.sharpness + 1) % (kMaxSharpnessLevel + 1));
            break;
        case command::ShowShortcuts:
            MessageBoxW(window_.handle(),
                        L"Échap\tQuitter\n"
                        L"F11\tPlein écran / fenêtre\n"
                        L"S\tMise à l'échelle : ajuster, 1:1, étirer\n"
                        L"R\tPlage de couleur : limitée / complète\n"
                        L"F\tAgrandissement : lissage GPU, bicubique, Lanczos\n"
                        L"N\tNetteté : aucune, légère, moyenne, forte\n"
                        L"Clic droit\tMenu (aussi en plein écran)",
                        L"HdmiScreen — raccourcis clavier", MB_OK | MB_ICONINFORMATION);
            return;
        case command::About: {
            // [C++] L"HdmiScreen " HDMISCREEN_VERSION : deux littéraux côte à côte
            // sont concaténés par le compilateur. La version vient de CMakeLists.txt.
            const std::wstring text = std::wstring(L"HdmiScreen " HDMISCREEN_VERSION L"\n\n") +
                                      L"Affiche en plein écran l'entrée d'un boîtier de capture HDMI.\n\n" +
                                      L"Réglages enregistrés dans :\n" + settingsPath_;
            MessageBoxW(window_.handle(), text.c_str(), L"À propos de HdmiScreen",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }
        case command::DeviceAuto:
            options_.deviceFilter.clear();
            restartCapture();
            break;
        case command::ModeAuto:
            options_.mode.reset();
            restartCapture();
            break;
        default:
            // Listes dynamiques : périphérique n°i ou mode n°i.
            if (id >= command::DeviceFirst && id < command::DeviceFirst + menuDevices_.size()) {
                options_.deviceFilter = menuDevices_[id - command::DeviceFirst].name;
                restartCapture();
            } else if (id >= command::ModeFirst && id < command::ModeFirst + menuModes_.size()) {
                options_.mode = menuModes_[id - command::ModeFirst];
                restartCapture();
            } else {
                return;   // commande inconnue
            }
            break;
    }
    persistSettings();
    refreshMenuBar();   // cases cochées à jour
    updateTitle();      // le titre indique aussi le filtre d'agrandissement
}

} // namespace hdmi
