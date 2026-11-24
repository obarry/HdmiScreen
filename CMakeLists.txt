# =============================================================================
# CMakeLists.txt — Description du build de HdmiScreen
# =============================================================================
#
# [C++ vs Java] CMake joue le rôle de Maven/Gradle pour la compilation, mais
# il ne télécharge rien : il GÉNÈRE un projet pour un outil de build
# (Visual Studio, Ninja, make). Toutes les bibliothèques utilisées ici font
# partie de Windows et du SDK Windows : aucune dépendance externe.
#
# Build (voir README.md) :
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#   ctest --test-dir build -C Release

cmake_minimum_required(VERSION 3.21)
project(HdmiScreen VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)            # C++20 : std::format, etc.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime C++ lié statiquement (MSVC /MT) : un seul .exe, rien à installer.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -----------------------------------------------------------------------------
# Logique pure (sans Windows) : bibliothèque partagée par l'appli et les tests
# -----------------------------------------------------------------------------
add_library(hdmiscreen_core STATIC
    src/Selection.cpp
    src/CommandLine.cpp
)
target_include_directories(hdmiscreen_core PUBLIC src)

# Options du compilateur, communes à toutes les cibles.
if(MSVC)
    # /W4 : avertissements élevés ; /utf-8 : sources et chaînes en UTF-8 ;
    # /permissive- : conformité stricte au standard.
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# Tests (compilables sur tout OS)
# -----------------------------------------------------------------------------
enable_testing()
add_executable(selection_tests tests/SelectionTests.cpp)
target_link_libraries(selection_tests PRIVATE hdmiscreen_core)
add_test(NAME selection_tests COMMAND selection_tests)

# -----------------------------------------------------------------------------
# L'application (Windows uniquement)
# -----------------------------------------------------------------------------
if(WIN32)
    # WIN32 : sous-système « fenêtré » (pas de console au double-clic),
    # point d'entrée wWinMain.
    add_executable(hdmiscreen WIN32
        src/main.cpp
        src/App.cpp
        src/CaptureDevices.cpp
        src/CaptureSession.cpp
        src/Log.cpp
        src/MainWindow.cpp
        src/Renderer.cpp
        src/SettingsFile.cpp
    )
    target_link_libraries(hdmiscreen PRIVATE
        hdmiscreen_core
        mfplat mf mfreadwrite mfuuid   # Media Foundation
        d3d11 dxgi                     # Direct3D 11 et DXGI
        shell32                        # CommandLineToArgvW
    )
    # UNICODE : les macros Win32 (CreateWindowEx…) pointent vers les versions « W ».
    target_compile_definitions(hdmiscreen PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        # Version affichée dans « À propos », tirée de project(... VERSION ...) ci-dessus.
        # L"..." : littéral « large » (wchar_t), directement utilisable par l'API Windows.
        HDMISCREEN_VERSION=L"${PROJECT_VERSION}")

    if(MINGW)
        # Compilation avec MinGW (GCC pour Windows) : point d'entrée Unicode et
        # bibliothèques C/C++ intégrées à l'exécutable.
        target_link_options(hdmiscreen PRIVATE -municode -static)
    endif()
endif()
