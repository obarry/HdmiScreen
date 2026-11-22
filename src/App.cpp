// =============================================================================
// App.cpp — Assemblage de l'application et boucle principale
// =============================================================================

#include "App.h"
#include "CaptureDevices.h"
#include "Log.h"

#include <format>

namespace hdmi {

namespace {

// [C++] « using namespace » limité à ce fichier : autorise 2s, 250ms…
using namespace std::chrono_literals;

constexpr auto kReconnectInterval = 2s;     // fréquence des tentatives d'ouverture
constexpr auto kNoSignalDelay = 1s;         // délai sans image avant « pas de signal »
constexpr DWORD kLoopTimeoutMs = 250;       // réveil minimal de la boucle

// Un périphérique ouvert et inspecté, candidat à la capture.
struct ProbedDevice {
    CaptureDeviceInfo info;
    ComPtr<IMFMediaSource> source;
    std::vector<VideoMode> modes;
    int score = 0;
};

// Ouvre tous les périphériques (filtrés par --device) et lit leurs modes.
// Les sources non retenues devront être fermées par shutdownAll.
std::vector<ProbedDevice> probeDevices(const Options& options) {
    std::vector<ProbedDevice> probed;
    for (const CaptureDeviceInfo& info : enumerateCaptureDevices()) {
        if (!options.deviceFilter.empty() && !containsIgnoreCase(info.name, options.deviceFilter)) {
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
            // Un périphérique occupé par une autre appli ne doit pas tout bloquer.
            log::error(std::format(L"{} : {}", info.name, widen(e.what())));
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

// Index du meilleur candidat (score le plus haut ; en cas d'égalité, le premier).
// Renvoie -1 si la liste est vide.
int bestDeviceIndex(const std::vector<ProbedDevice>& devices) {
    int best = -1;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (best < 0 || devices[i].score > devices[static_cast<size_t>(best)].score) {
            best = static_cast<int>(i);
        }
    }
    return best;
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
    std::vector<ProbedDevice> devices = probeDevices(options);
    const int best = bestDeviceIndex(devices);

    std::wstring text = std::format(L"Écran principal : {}x{}\n\n", screen.width, screen.height);
    if (devices.empty()) {
        text += L"Aucun périphérique de capture vidéo trouvé.\n";
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        const ProbedDevice& device = devices[i];
        const bool chosen = static_cast<int>(i) == best;
        text += std::format(L"[{}] {}  (score {}){}\n", i + 1, device.info.name, device.score,
                            chosen ? L"  <= périphérique retenu" : L"");
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

App::App(HINSTANCE instance, const Options& options)
    // [C++] Liste d'initialisation : construit les membres AVANT le corps du
    // constructeur, dans l'ordre de leur déclaration dans App.h.
    : options_(options),
      window_(instance, !options.windowed),
      // Événement à réinitialisation AUTOMATIQUE : il repasse à « non signalé »
      // dès qu'une attente l'a consommé.
      frameEvent_(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
      renderer_(std::make_unique<Renderer>(window_.handle())),
      screen_(primaryScreenSize()) {
    if (!frameEvent_) {
        throw HrError(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }
    renderer_->setScalingMode(options.scaling);
    renderer_->setFullRange(options.fullRange);

    // Brancher les actions de la fenêtre.
    // [C++] Le lambda capture « this » : il ne doit pas survivre à l'objet App
    // (garanti ici, la fenêtre étant un membre détruit avec App).
    window_.onResize = [this](uint32_t width, uint32_t height) { renderer_->resize(width, height); };
    window_.onToggleScaling = [this]() {
        switch (renderer_->scalingMode()) {
            case ScalingMode::Fit:      renderer_->setScalingMode(ScalingMode::OneToOne); break;
            case ScalingMode::OneToOne: renderer_->setScalingMode(ScalingMode::Stretch); break;
            case ScalingMode::Stretch:  renderer_->setScalingMode(ScalingMode::Fit); break;
        }
    };

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
    std::vector<ProbedDevice> devices = probeDevices(options_);
    const int best = bestDeviceIndex(devices);
    if (best < 0) {
        log::info(L"Aucun périphérique de capture : nouvelle tentative dans 2 s");
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
    lastFrameTime_ = Clock::now();
    return true;
}

void App::closeCapture() {
    if (session_) {
        session_->stop();
        session_.Reset();
    }
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
            log::error(std::format(L"Capture interrompue (HRESULT 0x{:08X})",
                                   static_cast<unsigned long>(session_->error())));
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

} // namespace hdmi
