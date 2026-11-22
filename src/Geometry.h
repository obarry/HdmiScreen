// =============================================================================
// Geometry.h — Calcul du rectangle d'affichage (bandes noires)
// =============================================================================
//
// Logique pure, sans Windows, testée dans tests/SelectionTests.cpp.

#pragma once

#include <cstdint>

namespace hdmi {

// Rectangle en pixels : [left, right) × [top, bottom), comme le RECT de Win32.
struct Rect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    int32_t width() const { return right - left; }
    int32_t height() const { return bottom - top; }
};

// Mode de mise à l'échelle de l'image capturée dans la fenêtre.
enum class ScalingMode {
    Fit,        // agrandir au maximum en gardant les proportions (bandes noires)
    OneToOne,   // pixel pour pixel, centré (net mais petit si l'écran est plus grand)
    Stretch     // remplir toute la fenêtre, quitte à déformer l'image
};

// Calcule où dessiner une image source (srcW × srcH) dans une cible (dstW × dstH).
//
// Exemple du projet : 1920×1080 dans 2880×1800 en mode Fit
//   facteur = min(2880/1920, 1800/1080) = min(1,5 ; 1,667) = 1,5
//   image   = 2880×1620, centrée verticalement => bandes de 90 px en haut et en bas
//
// [C++] « constexpr » : la fonction PEUT être évaluée à la compilation si ses
// arguments sont des constantes. Elle reste utilisable normalement à l'exécution.
constexpr Rect computeDestinationRect(int32_t srcW, int32_t srcH, int32_t dstW, int32_t dstH,
                                      ScalingMode mode) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return Rect{0, 0, dstW, dstH};
    }
    int32_t w = dstW;
    int32_t h = dstH;
    switch (mode) {
        case ScalingMode::Stretch:
            break;
        case ScalingMode::OneToOne:
            w = srcW < dstW ? srcW : dstW;
            h = srcH < dstH ? srcH : dstH;
            break;
        case ScalingMode::Fit:
            // On compare dstW/srcW et dstH/srcH par produit en croix, en 64 bits,
            // pour éviter les flottants et leurs arrondis.
            if (static_cast<int64_t>(dstW) * srcH <= static_cast<int64_t>(dstH) * srcW) {
                // La largeur est limitante : pleine largeur, hauteur proportionnelle.
                w = dstW;
                h = static_cast<int32_t>(static_cast<int64_t>(srcH) * dstW / srcW);
            } else {
                // La hauteur est limitante.
                h = dstH;
                w = static_cast<int32_t>(static_cast<int64_t>(srcW) * dstH / srcH);
            }
            break;
    }
    const int32_t left = (dstW - w) / 2;
    const int32_t top = (dstH - h) / 2;
    return Rect{left, top, left + w, top + h};
}

} // namespace hdmi
