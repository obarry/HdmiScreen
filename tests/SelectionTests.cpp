// =============================================================================
// SelectionTests.cpp — Tests de la logique pure (choix de mode, géométrie, options)
// =============================================================================
//
// [C++ vs Java] Il n'y a pas de JUnit dans la bibliothèque standard C++. Des
// frameworks existent (GoogleTest, Catch2, doctest), mais pour rester sans
// dépendance on utilise ici une macro CHECK minimaliste. Le programme renvoie
// 0 si tout passe, 1 sinon : c'est ce que regarde CTest (« ctest » après le build).
//
// Ces tests compilent sur n'importe quel OS, sans Windows.

#include "../src/CommandLine.h"
#include "../src/Geometry.h"
#include "../src/Selection.h"

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

// [C++] Une macro est un remplacement de texte fait AVANT la compilation.
// Elle permet ici d'afficher l'expression testée (#expr la transforme en texte)
// ainsi que le fichier et la ligne. On les évite en code normal.
#define CHECK(expr)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(expr)) {                                                           \
            ++g_failures;                                                        \
            std::printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #expr);        \
        }                                                                        \
    } while (false)

using namespace hdmi;   // [C++] équivalent d'un import du package entier

// Petite fabrique pour écrire les modes de façon compacte.
VideoMode mode(uint32_t w, uint32_t h, uint32_t fps, PixelFormat f, uint32_t index = 0) {
    VideoMode m;
    m.width = w;
    m.height = h;
    m.fpsNumerator = fps;
    m.fpsDenominator = 1;
    m.format = f;
    m.nativeIndex = index;
    return m;
}

bool sameMode(const std::optional<VideoMode>& m, uint32_t w, uint32_t h, PixelFormat f) {
    return m && m->width == w && m->height == h && m->format == f;   // m-> : membre via optional
}

// --- Choix du mode -----------------------------------------------------------

void testPrefers1610OnThePersonalLaptop() {
    // Écran du portable perso : 2880×1800 (16:10).
    const ScreenSize screen{2880, 1800};
    const std::vector<VideoMode> modes = {
        mode(1920, 1080, 60, PixelFormat::Yuy2),
        mode(1920, 1080, 60, PixelFormat::Mjpeg),
        mode(1920, 1200, 60, PixelFormat::Yuy2),
        mode(3840, 2160, 30, PixelFormat::Mjpeg),   // plus grand que l'écran
        mode(1280, 720, 60, PixelFormat::Nv12),
    };
    CHECK(sameMode(selectBestMode(modes, screen), 1920, 1200, PixelFormat::Yuy2));
}

void testFallsBackTo1080pUncompressed() {
    const ScreenSize screen{2880, 1800};
    const std::vector<VideoMode> modes = {
        mode(1920, 1080, 60, PixelFormat::Mjpeg),
        mode(1920, 1080, 60, PixelFormat::Yuy2),
        mode(1280, 720, 60, PixelFormat::Nv12),
    };
    CHECK(sameMode(selectBestMode(modes, screen), 1920, 1080, PixelFormat::Yuy2));
}

void testExactScreenMatchWins() {
    const ScreenSize screen{1920, 1080};
    const std::vector<VideoMode> modes = {
        mode(1280, 720, 60, PixelFormat::Nv12),
        mode(1920, 1080, 30, PixelFormat::Mjpeg),
    };
    CHECK(sameMode(selectBestMode(modes, screen), 1920, 1080, PixelFormat::Mjpeg));
}

void testFluidityBeforeFormat() {
    // À taille égale, 60 i/s compressé est préféré à 30 i/s non compressé.
    const ScreenSize screen{2880, 1800};
    const std::vector<VideoMode> modes = {
        mode(1920, 1080, 30, PixelFormat::Yuy2),
        mode(1920, 1080, 60, PixelFormat::Mjpeg),
    };
    CHECK(sameMode(selectBestMode(modes, screen), 1920, 1080, PixelFormat::Mjpeg));
}

void testNtscRateCountsAs60() {
    const ScreenSize screen{2880, 1800};
    VideoMode ntsc = mode(1920, 1080, 0, PixelFormat::Nv12);
    ntsc.fpsNumerator = 60000;
    ntsc.fpsDenominator = 1001;   // 59,94 i/s
    const std::vector<VideoMode> modes = {mode(1920, 1080, 60, PixelFormat::Yuy2), ntsc};
    // 59,94 ≈ 60, donc le format départage : NV12 l'emporte.
    CHECK(sameMode(selectBestMode(modes, screen), 1920, 1080, PixelFormat::Nv12));
}

void testIgnoresUnknownFormatsAndEmptyLists() {
    const ScreenSize screen{2880, 1800};
    CHECK(!selectBestMode({}, screen));
    CHECK(!selectBestMode({mode(1920, 1080, 60, PixelFormat::Other)}, screen));
}

void testFindModeHonoursRequest() {
    const ScreenSize screen{2880, 1800};
    const std::vector<VideoMode> modes = {
        mode(1920, 1080, 60, PixelFormat::Mjpeg),
        mode(1920, 1080, 30, PixelFormat::Yuy2),
        mode(1920, 1080, 60, PixelFormat::Yuy2),
    };
    CHECK(sameMode(findMode(modes, 1920, 1080, 30, screen), 1920, 1080, PixelFormat::Yuy2));
    CHECK(findMode(modes, 1920, 1080, 30, screen)->fps() == 30.0);
    CHECK(sameMode(findMode(modes, 1920, 1080, 0, screen), 1920, 1080, PixelFormat::Yuy2));
    CHECK(!findMode(modes, 1920, 1200, 0, screen));
}

// --- Choix du périphérique ---------------------------------------------------

void testDeviceScoring() {
    const std::vector<VideoMode> captureModes = {mode(1920, 1080, 60, PixelFormat::Yuy2)};
    const std::vector<VideoMode> webcamModes = {mode(1920, 1080, 30, PixelFormat::Mjpeg)};
    const int box = scoreCaptureDevice(L"USB3 Video", captureModes);
    const int webcam = scoreCaptureDevice(L"Integrated Camera", webcamModes);
    CHECK(box == 3);
    CHECK(webcam == 0);
    CHECK(containsIgnoreCase(L"USB3 Video", L"usb3"));
    CHECK(!containsIgnoreCase(L"USB3 Video", L"camera"));
}

void testWebcamIsNeverChosenAutomatically() {
    // Cas réel : seule la webcam du portable est présente (boîtier débranché).
    const std::vector<VideoMode> webcamModes = {mode(2560, 1440, 30, PixelFormat::Nv12)};
    const int webcamScore = scoreCaptureDevice(L"HP 5MP Camera", webcamModes);
    CHECK(webcamScore < kMinimumAutoScore);
    CHECK(chooseDevice({{L"webcam", webcamScore}}, L"", false) == -1);
    // Avec --device, l'utilisateur a choisi : on accepte.
    CHECK(chooseDevice({{L"webcam", webcamScore}}, L"", true) == 0);
}

void testBoxIsPreferredOverWebcam() {
    const std::vector<DeviceCandidate> candidates = {{L"webcam", 1}, {L"box", 3}};
    CHECK(chooseDevice(candidates, L"", false) == 1);
}

void testSameDeviceAfterMicroCut() {
    // Deux boîtiers ; on utilisait le second : après une coupure, on y revient
    // même si l'autre a un meilleur score.
    const std::vector<DeviceCandidate> candidates = {{L"box-a", 3}, {L"box-b", 2}};
    CHECK(chooseDevice(candidates, L"box-b", false) == 1);
    // S'il n'est pas (encore) revenu, on prend le meilleur acceptable.
    CHECK(chooseDevice({{L"box-a", 3}}, L"box-b", false) == 0);
    CHECK(chooseDevice({}, L"box-b", false) == -1);
}

// --- Géométrie ---------------------------------------------------------------

void testFitLetterbox() {
    // Le cas du projet : 1080p sur un écran 2880×1800 => bandes de 90 px.
    const Rect r = computeDestinationRect(1920, 1080, 2880, 1800, ScalingMode::Fit);
    CHECK(r.left == 0 && r.top == 90 && r.right == 2880 && r.bottom == 1710);

    // 16:10 sur 16:10 : plein écran, facteur 1,5.
    const Rect full = computeDestinationRect(1920, 1200, 2880, 1800, ScalingMode::Fit);
    CHECK(full.left == 0 && full.top == 0 && full.width() == 2880 && full.height() == 1800);

    // Image verticale dans un écran paysage => bandes sur les côtés.
    const Rect pillar = computeDestinationRect(1080, 1920, 1920, 1080, ScalingMode::Fit);
    CHECK(pillar.top == 0 && pillar.height() == 1080 && pillar.width() == 607);
}

void testOneToOneAndStretch() {
    const Rect one = computeDestinationRect(1920, 1080, 2880, 1800, ScalingMode::OneToOne);
    CHECK(one.left == 480 && one.top == 360 && one.width() == 1920 && one.height() == 1080);
    const Rect stretch = computeDestinationRect(1920, 1080, 2880, 1800, ScalingMode::Stretch);
    CHECK(stretch.left == 0 && stretch.top == 0 && stretch.width() == 2880);
}

// [C++] static_assert : vérification faite PENDANT la compilation, possible
// parce que computeDestinationRect est constexpr.
static_assert(computeDestinationRect(1920, 1080, 2880, 1800, ScalingMode::Fit).top == 90);

// --- Ligne de commande -------------------------------------------------------

void testCommandLine() {
    const ParseResult ok = parseArguments({L"--device", L"USB3", L"--mode", L"1920x1200@60",
                                           L"--scaling", L"1to1", L"--windowed"});
    CHECK(ok.ok());
    CHECK(ok.options.deviceFilter == L"USB3");
    CHECK(ok.options.mode && ok.options.mode->width == 1920 && ok.options.mode->height == 1200 &&
          ok.options.mode->fps == 60);
    CHECK(ok.options.scaling == ScalingMode::OneToOne);
    CHECK(ok.options.windowed);

    CHECK(parseModeText(L"1920x1080") && parseModeText(L"1920x1080")->fps == 0);
    CHECK(!parseModeText(L"1920*1080"));
    CHECK(!parseModeText(L"x1080"));
    CHECK(!parseModeText(L"1920x1080@"));
    CHECK(!parseArguments({L"--mode"}).ok());
    CHECK(!parseArguments({L"--bogus"}).ok());
    CHECK(!parseArguments({L"--scaling", L"zoom"}).ok());
}

} // namespace

int main() {
    testPrefers1610OnThePersonalLaptop();
    testFallsBackTo1080pUncompressed();
    testExactScreenMatchWins();
    testFluidityBeforeFormat();
    testNtscRateCountsAs60();
    testIgnoresUnknownFormatsAndEmptyLists();
    testFindModeHonoursRequest();
    testDeviceScoring();
    testWebcamIsNeverChosenAutomatically();
    testBoxIsPreferredOverWebcam();
    testSameDeviceAfterMicroCut();
    testFitLetterbox();
    testOneToOneAndStretch();
    testCommandLine();

    std::printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
