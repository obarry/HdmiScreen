// =============================================================================
// CaptureDevices.h — Découverte des périphériques vidéo et de leurs modes
// =============================================================================
//
// Utilise Media Foundation (MF), l'API multimédia de Windows. Concepts :
// docs/API_WINDOWS.md §3. Les noms d'API en commentaire sont des points
// d'entrée vers la documentation Microsoft (learn.microsoft.com).

#pragma once

#include "ComUtils.h"
#include "VideoMode.h"

#include <mfidl.h>     // [API] IMFMediaSource, MFEnumDeviceSources

#include <string>
#include <vector>

namespace hdmi {

// Démarre et arrête Media Foundation (RAII, comme ComInitializer).
// [API] MFStartup / MFShutdown : obligatoires avant/après tout usage de MF.
class MediaFoundationInitializer {
public:
    MediaFoundationInitializer();
    ~MediaFoundationInitializer();
    MediaFoundationInitializer(const MediaFoundationInitializer&) = delete;
    MediaFoundationInitializer& operator=(const MediaFoundationInitializer&) = delete;
};

// Ce qu'on sait d'un périphérique avant de l'ouvrir.
struct CaptureDeviceInfo {
    std::wstring name;           // nom lisible, ex. « USB3 Video »
    std::wstring symbolicLink;   // identifiant unique et stable du périphérique
};

// Liste les périphériques de capture vidéo (webcams, boîtiers UVC…).
// [API] MFEnumDeviceSources avec MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID.
std::vector<CaptureDeviceInfo> enumerateCaptureDevices();

// Ouvre un périphérique et renvoie sa « source média ».
// [API] MFCreateDeviceSource. La source ne capture rien tant qu'un lecteur
// (IMFSourceReader) ne la démarre pas.
ComPtr<IMFMediaSource> openDevice(const CaptureDeviceInfo& device);

// Liste les modes que le périphérique sait produire (ses « types natifs »).
// [API] IMFPresentationDescriptor -> IMFStreamDescriptor -> IMFMediaTypeHandler.
std::vector<VideoMode> listNativeModes(IMFMediaSource* source);

// Configure le périphérique sur l'un de ses modes natifs.
// [API] IMFMediaTypeHandler::SetCurrentMediaType.
void applyNativeMode(IMFMediaSource* source, const VideoMode& mode);

} // namespace hdmi
