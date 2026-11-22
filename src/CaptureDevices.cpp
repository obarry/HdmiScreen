// =============================================================================
// CaptureDevices.cpp — Découverte des périphériques vidéo et de leurs modes
// =============================================================================

#include "CaptureDevices.h"

#include <mfapi.h>     // [API] MFStartup, MFCreateAttributes, GUID des formats vidéo

namespace hdmi {

MediaFoundationInitializer::MediaFoundationInitializer() {
    // MF_VERSION : version de MF attendue par notre programme (vérifiée par Windows).
    throwIfFailed(MFStartup(MF_VERSION), "MFStartup");
}

MediaFoundationInitializer::~MediaFoundationInitializer() {
    MFShutdown();
}

namespace {

// Lit le premier flux (stream) vidéo d'une source et renvoie son
// « gestionnaire de types », qui connaît la liste des modes natifs.
//
// [API] Hiérarchie Media Foundation :
//   IMFMediaSource                 le périphérique
//    └ IMFPresentationDescriptor   la description de tous ses flux
//       └ IMFStreamDescriptor      un flux (ici : la vidéo)
//          └ IMFMediaTypeHandler   les formats possibles de ce flux
ComPtr<IMFMediaTypeHandler> firstStreamTypeHandler(IMFMediaSource* source) {
    ComPtr<IMFPresentationDescriptor> presentation;
    // [C++] &presentation : ComPtr surcharge l'opérateur & pour fournir
    // l'adresse de son pointeur interne ; l'API y dépose l'objet créé (déjà AddRef).
    throwIfFailed(source->CreatePresentationDescriptor(&presentation),
                  "IMFMediaSource::CreatePresentationDescriptor");

    BOOL selected = FALSE;   // BOOL (Win32, 32 bits) n'est pas bool (C++, 1 octet)
    ComPtr<IMFStreamDescriptor> stream;
    throwIfFailed(presentation->GetStreamDescriptorByIndex(0, &selected, &stream),
                  "IMFPresentationDescriptor::GetStreamDescriptorByIndex");

    ComPtr<IMFMediaTypeHandler> handler;
    throwIfFailed(stream->GetMediaTypeHandler(&handler), "IMFStreamDescriptor::GetMediaTypeHandler");
    return handler;
}

// Traduit le GUID de sous-type MF en notre enum.
// [API] Un GUID est un identifiant 128 bits ; MF s'en sert pour nommer les formats.
PixelFormat toPixelFormat(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12) return PixelFormat::Nv12;
    if (subtype == MFVideoFormat_YUY2) return PixelFormat::Yuy2;
    if (subtype == MFVideoFormat_MJPG) return PixelFormat::Mjpeg;
    return PixelFormat::Other;
}

// Lit une chaîne d'un IMFActivate (nom, lien symbolique…).
std::wstring readString(IMFActivate* activate, const GUID& key) {
    CoTaskString text;
    UINT32 length = 0;
    // [API] GetAllocatedString alloue la chaîne avec CoTaskMemAlloc :
    // c'est à nous de la libérer (CoTaskString le fait dans son destructeur).
    if (FAILED(activate->GetAllocatedString(key, text.put(), &length))) {
        return {};
    }
    return text.str();
}

} // namespace

std::vector<CaptureDeviceInfo> enumerateCaptureDevices() {
    // [API] Un « attribut » MF est un couple clé (GUID) / valeur. On demande ici
    // les sources de type « capture vidéo ».
    ComPtr<IMFAttributes> filter;
    throwIfFailed(MFCreateAttributes(&filter, 1), "MFCreateAttributes");
    throwIfFailed(filter->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                  "IMFAttributes::SetGUID");

    IMFActivate** devices = nullptr;   // tableau C alloué par l'API
    UINT32 count = 0;
    throwIfFailed(MFEnumDeviceSources(filter.Get(), &devices, &count), "MFEnumDeviceSources");

    // [C++ vs Java] Gestion mémoire « à la main » imposée par l'API C :
    // chaque élément doit être libéré (Release) PUIS le tableau (CoTaskMemFree).
    // On copie d'abord ce qui nous intéresse dans des objets C++ ordinaires.
    std::vector<CaptureDeviceInfo> result;
    for (UINT32 i = 0; i < count; ++i) {
        result.push_back({readString(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME),
                          readString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)});
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return result;
}

ComPtr<IMFMediaSource> openDevice(const CaptureDeviceInfo& device) {
    // On désigne le périphérique par son lien symbolique (stable), pas par son
    // rang dans la liste (qui peut changer si on branche une autre caméra).
    ComPtr<IMFAttributes> attributes;
    throwIfFailed(MFCreateAttributes(&attributes, 2), "MFCreateAttributes");
    throwIfFailed(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                  "IMFAttributes::SetGUID");
    throwIfFailed(attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                        device.symbolicLink.c_str()),
                  "IMFAttributes::SetString");

    ComPtr<IMFMediaSource> source;
    throwIfFailed(MFCreateDeviceSource(attributes.Get(), &source), "MFCreateDeviceSource");
    return source;   // ComPtr se « déplace » : pas de AddRef/Release superflu
}

std::vector<VideoMode> listNativeModes(IMFMediaSource* source) {
    ComPtr<IMFMediaTypeHandler> handler = firstStreamTypeHandler(source);

    DWORD typeCount = 0;
    throwIfFailed(handler->GetMediaTypeCount(&typeCount), "IMFMediaTypeHandler::GetMediaTypeCount");

    std::vector<VideoMode> modes;
    for (DWORD i = 0; i < typeCount; ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(handler->GetMediaTypeByIndex(i, &type))) {
            continue;   // un type illisible n'empêche pas de lire les autres
        }
        VideoMode mode;
        mode.nativeIndex = i;

        GUID subtype{};
        if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
            mode.format = toPixelFormat(subtype);
        }
        // [API] La taille et la fréquence sont stockées comme deux entiers 32 bits
        // empaquetés dans un 64 bits ; ces fonctions utilitaires les extraient.
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &mode.width, &mode.height);
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &mode.fpsNumerator, &mode.fpsDenominator);
        modes.push_back(mode);
    }
    return modes;
}

void applyNativeMode(IMFMediaSource* source, const VideoMode& mode) {
    ComPtr<IMFMediaTypeHandler> handler = firstStreamTypeHandler(source);
    ComPtr<IMFMediaType> type;
    throwIfFailed(handler->GetMediaTypeByIndex(mode.nativeIndex, &type),
                  "IMFMediaTypeHandler::GetMediaTypeByIndex");
    // Le boîtier passera dans ce mode au démarrage de la capture.
    throwIfFailed(handler->SetCurrentMediaType(type.Get()), "IMFMediaTypeHandler::SetCurrentMediaType");
}

} // namespace hdmi
