// =============================================================================
// VideoMode.h — Description d'un mode vidéo proposé par le boîtier de capture
// =============================================================================
//
// Ce fichier ne dépend PAS de Windows : il est compilable et testable sur
// n'importe quelle plateforme (voir tests/SelectionTests.cpp).
//
// [C++ vs Java] Un fichier .h (« header ») contient des DÉCLARATIONS que
// d'autres fichiers incluent avec #include. Le compilateur C++ compile chaque
// .cpp séparément et ne « voit » que ce qui a été inclus : il n'y a pas
// d'équivalent au classpath Java qui trouve les classes tout seul.
// Voir docs/CPP_POUR_DEV_JAVA.md §1.

// [C++] #pragma once : empêche d'inclure deux fois ce fichier dans le même
// .cpp (sinon, double définition => erreur de compilation).
#pragma once

#include <cstdint>   // uint32_t, uint64_t : entiers de taille garantie
#include <string>    // std::wstring

namespace hdmi {     // [C++ vs Java] un namespace ~ un package, sans lien avec les dossiers

// -----------------------------------------------------------------------------
// Format des pixels envoyés par le boîtier sur l'USB.
// -----------------------------------------------------------------------------
// [C++ vs Java] « enum class » ressemble à un enum Java, mais ce n'est qu'un
// entier nommé : pas de méthodes, pas de champs. On l'écrit PixelFormat::Nv12.
enum class PixelFormat {
    Nv12,   // YUV 4:2:0 planaire, 1,5 octet/pixel : le format natif des GPU
    Yuy2,   // YUV 4:2:2 entrelacé, 2 octets/pixel : non compressé, très répandu en UVC
    Mjpeg,  // Motion JPEG : compressé, à décoder ; artefacts sur le texte
    Other   // tout le reste (RGB24, H.264…) : ignoré par l'utilitaire
};

// [C++] « inline » permet de DÉFINIR une fonction dans un header sans erreur
// de double définition quand plusieurs .cpp l'incluent.
// Les fonctions libres (hors classe) n'existent pas en Java : ici, pas besoin
// d'une classe utilitaire statique.
inline const wchar_t* toString(PixelFormat format) {
    switch (format) {
        case PixelFormat::Nv12:  return L"NV12";   // L"..." = chaîne « large » (UTF-16 sous Windows)
        case PixelFormat::Yuy2:  return L"YUY2";
        case PixelFormat::Mjpeg: return L"MJPEG";
        case PixelFormat::Other: break;
    }
    return L"autre";
}

// -----------------------------------------------------------------------------
// Un mode de capture : résolution, fréquence d'images, format.
// -----------------------------------------------------------------------------
// [C++ vs Java] Une « struct » est une classe dont les membres sont publics par
// défaut. C'est un TYPE VALEUR : « VideoMode a = b; » COPIE les champs, alors
// qu'en Java « a = b » copierait une référence vers le même objet.
struct VideoMode {
    // [C++] Initialiser chaque champ ici évite les valeurs indéterminées :
    // contrairement à Java, un int non initialisé contient n'importe quoi.
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fpsNumerator = 0;     // la fréquence est une fraction : 60000/1001 = 59,94 i/s
    uint32_t fpsDenominator = 1;
    PixelFormat format = PixelFormat::Other;

    // Position de ce mode dans la liste native du périphérique, pour pouvoir
    // le resélectionner ensuite (IMFMediaTypeHandler::GetMediaTypeByIndex).
    uint32_t nativeIndex = 0;

    // [C++] « const » après la signature = cette méthode ne modifie pas l'objet.
    // Le compilateur le vérifie ; il n'y a pas d'équivalent en Java.
    double fps() const {
        return fpsDenominator == 0 ? 0.0
                                   : static_cast<double>(fpsNumerator) / fpsDenominator;
    }

    // Nombre de pixels. uint64_t car 3840 × 2160 tient dans 32 bits, mais les
    // produits croisés utilisés plus loin non.
    uint64_t area() const { return static_cast<uint64_t>(width) * height; }
};

// Texte lisible, ex. « 1920x1080 @ 60 i/s YUY2 ». Défini dans Selection.cpp.
std::wstring describe(const VideoMode& mode);

} // namespace hdmi
