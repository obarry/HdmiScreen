// =============================================================================
// CaptureSession.cpp — Capture asynchrone des images du boîtier
// =============================================================================

#include "CaptureSession.h"
#include "CaptureDevices.h"
#include "Log.h"

#include <mfapi.h>
#include <mferror.h>   // [API] codes d'erreur MF_E_*

#include <vector>

namespace hdmi {

// -----------------------------------------------------------------------------
// Création et configuration
// -----------------------------------------------------------------------------

CaptureSession::CaptureSession(HANDLE frameEvent)
    : frameEvent_(frameEvent),
      // Événement « manuel » non signalé : on l'attend dans stop().
      flushDone_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

CaptureSession::~CaptureSession() = default;   // [C++] destructeur généré par le compilateur

ComPtr<CaptureSession> CaptureSession::create(IMFMediaSource* source,
                                              IMFDXGIDeviceManager* deviceManager,
                                              const VideoMode& mode, HANDLE frameEvent) {
    // [C++ vs Java] « new » alloue sur le tas, comme en Java, mais rien ne
    // libérera l'objet automatiquement : c'est le comptage de références COM
    // (Release) qui fera « delete this ».
    //
    // Le constructeur démarre le compteur à 1 ; Attach() confie cette
    // référence au ComPtr SANS appeler AddRef (sinon on aurait 2 et une fuite).
    ComPtr<CaptureSession> session;
    session.Attach(new CaptureSession(frameEvent));
    try {
        session->configureReader(source, deviceManager, mode);
    } catch (...) {
        // [C++] catch (...) attrape TOUT. Si la configuration échoue après la
        // création du lecteur, celui-ci garde une référence sur la session et
        // la session sur lui : sans stop(), aucun des deux ne serait libéré.
        session->stop();
        throw;   // relance la même exception, comme « throw e; » en Java
    }
    return session;
}

void CaptureSession::configureReader(IMFMediaSource* source, IMFDXGIDeviceManager* deviceManager,
                                     const VideoMode& mode) {
    // 1. Mettre le boîtier dans le mode choisi (résolution, fréquence, format).
    applyNativeMode(source, mode);

    // 2. Créer le lecteur (Source Reader) avec ses options.
    ComPtr<IMFAttributes> attributes;
    throwIfFailed(MFCreateAttributes(&attributes, 4), "MFCreateAttributes");

    // [API] MF_SOURCE_READER_ASYNC_CALLBACK : mode ASYNCHRONE. ReadSample() rend
    // la main tout de suite, et MF appelle OnReadSample() quand l'image arrive.
    // « this » est converti en IUnknown* ; le lecteur garde une référence sur nous.
    throwIfFailed(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,
                                         static_cast<IMFSourceReaderCallback*>(this)),
                  "SetUnknown(ASYNC_CALLBACK)");

    // [API] MF_SOURCE_READER_D3D_MANAGER : le lecteur peut produire des images
    // directement en mémoire GPU (textures Direct3D) quand un décodeur matériel
    // est utilisé (cas du MJPEG).
    throwIfFailed(attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, deviceManager),
                  "SetUnknown(D3D_MANAGER)");

    // [API] Autorise les décodeurs matériels (GPU) plutôt que logiciels.
    throwIfFailed(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
                  "SetUINT32(ENABLE_HARDWARE_TRANSFORMS)");

    // [API] Demande aux composants de privilégier la latence au débit
    // (pas de mise en tampon de plusieurs images).
    throwIfFailed(attributes->SetUINT32(MF_LOW_LATENCY, TRUE), "SetUINT32(LOW_LATENCY)");

    throwIfFailed(MFCreateSourceReaderFromMediaSource(source, attributes.Get(), &reader_),
                  "MFCreateSourceReaderFromMediaSource");

    // 3. Ne lire que le flux vidéo (ignorer un éventuel flux audio).
    throwIfFailed(reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE),
                  "SetStreamSelection(ALL)");
    throwIfFailed(reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE),
                  "SetStreamSelection(VIDEO)");

    // 4. Choisir le format de SORTIE du lecteur.
    //    - NV12 / YUY2 natifs : on garde le format tel quel (aucune conversion).
    //    - MJPEG : on demande NV12 (ou YUY2 à défaut) ; le lecteur insère alors
    //      automatiquement un décodeur MJPEG entre le boîtier et nous.
    std::vector<GUID> candidates;
    switch (mode.format) {
        case PixelFormat::Nv12: candidates = {MFVideoFormat_NV12}; break;
        case PixelFormat::Yuy2: candidates = {MFVideoFormat_YUY2}; break;
        default:                candidates = {MFVideoFormat_NV12, MFVideoFormat_YUY2}; break;
    }

    HRESULT hr = E_FAIL;
    for (const GUID& subtype : candidates) {
        ComPtr<IMFMediaType> outputType;
        throwIfFailed(MFCreateMediaType(&outputType), "MFCreateMediaType");
        throwIfFailed(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "SetGUID(MAJOR_TYPE)");
        throwIfFailed(outputType->SetGUID(MF_MT_SUBTYPE, subtype), "SetGUID(SUBTYPE)");
        throwIfFailed(MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, mode.width, mode.height),
                      "MFSetAttributeSize");
        hr = reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                          nullptr, outputType.Get());
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    throwIfFailed(hr, "IMFSourceReader::SetCurrentMediaType (aucun format de sortie accepté)");

    // 5. Relire le format réellement obtenu : c'est lui que le rendu doit connaître.
    ComPtr<IMFMediaType> actual;
    throwIfFailed(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                               &actual),
                  "IMFSourceReader::GetCurrentMediaType");
    GUID subtype{};
    throwIfFailed(actual->GetGUID(MF_MT_SUBTYPE, &subtype), "GetGUID(SUBTYPE)");
    throwIfFailed(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &frameFormat_.width,
                                     &frameFormat_.height),
                  "MFGetAttributeSize");
    frameFormat_.format = subtype == MFVideoFormat_NV12   ? PixelFormat::Nv12
                          : subtype == MFVideoFormat_YUY2 ? PixelFormat::Yuy2
                                                          : PixelFormat::Other;
}

// -----------------------------------------------------------------------------
// Cycle de vie
// -----------------------------------------------------------------------------

void CaptureSession::start() {
    // [C++] std::lock_guard verrouille le mutex dans son constructeur et le
    // déverrouille dans son destructeur (RAII) : équivalent d'un bloc synchronized.
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = true;
    requestNextSample();
}

void CaptureSession::requestNextSample() {
    if (stopping_ || !reader_) {
        return;
    }
    // [API] En mode asynchrone, tous les paramètres de sortie sont nuls :
    // le résultat arrivera dans OnReadSample.
    const HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                           0, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) {
        fail(hr);
    }
}

void CaptureSession::stop() {
    ComPtr<IMFSourceReader> reader;
    bool started = false;
    {   // [C++] bloc pour limiter la durée du verrou
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;   // plus aucune nouvelle demande d'image après ceci
        reader = reader_;   // copie du ComPtr => AddRef
        started = started_;
    }

    if (reader && started) {
        // [API] Flush annule la demande en cours ; MF appelle ensuite OnFlush.
        // On attend cet appel (2 s max) pour être sûr qu'aucun callback ne
        // s'exécute encore quand on libère le lecteur.
        ResetEvent(flushDone_.get());
        if (SUCCEEDED(reader->Flush(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM)))) {
            WaitForSingleObject(flushDone_.get(), 2000);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    latestSample_.Reset();
    // Libérer le lecteur casse le cycle de références (lecteur -> callback -> lecteur)
    // et, par défaut, arrête le périphérique (la LED du boîtier s'éteint).
    reader_.Reset();
}

ComPtr<IMFSample> CaptureSession::takeLatestSample() {
    std::lock_guard<std::mutex> lock(mutex_);
    // [C++] std::move transfère le contenu (le pointeur) sans AddRef/Release :
    // latestSample_ devient vide. Voir docs/CPP_POUR_DEV_JAVA.md §5.
    return std::move(latestSample_);
}

void CaptureSession::fail(HRESULT hr) {
    error_.store(hr);
    SetEvent(frameEvent_);   // réveille la boucle UI pour qu'elle constate l'erreur
}

// -----------------------------------------------------------------------------
// IUnknown
// -----------------------------------------------------------------------------
// [API] Les trois méthodes que TOUT objet COM doit fournir. C'est ce que
// ComPtr appelle pour nous côté « client » ; ici on est côté « serveur ».

STDMETHODIMP CaptureSession::QueryInterface(REFIID iid, void** object) {
    // QueryInterface ~ un « instanceof + cast » : l'appelant demande une
    // interface par son identifiant (IID) et on renvoie le bon pointeur.
    if (object == nullptr) {
        return E_POINTER;
    }
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
        *object = static_cast<IMFSourceReaderCallback*>(this);
        AddRef();   // règle COM : tout pointeur renvoyé est déjà compté
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CaptureSession::AddRef() {
    return ++refCount_;   // atomique : peut être appelé depuis plusieurs threads
}

STDMETHODIMP_(ULONG) CaptureSession::Release() {
    const ULONG count = --refCount_;
    if (count == 0) {
        // [C++] « delete this » : l'objet se détruit lui-même. Légal et
        // classique en COM, à condition de ne plus toucher aucun membre ensuite.
        delete this;
    }
    return count;
}

// -----------------------------------------------------------------------------
// IMFSourceReaderCallback — exécutées sur des threads de Media Foundation
// -----------------------------------------------------------------------------
// [C++] RÈGLE ESSENTIELLE : une exception ne doit JAMAIS sortir d'une méthode
// appelée par Windows (callback COM, procédure de fenêtre). Le code appelant
// est du C et ne sait pas les gérer : ce serait un plantage. Ici, on n'appelle
// que des fonctions qui renvoient des HRESULT, sans throwIfFailed.

STDMETHODIMP CaptureSession::OnReadSample(HRESULT status, DWORD /*streamIndex*/, DWORD streamFlags,
                                          LONGLONG /*timestamp*/, IMFSample* sample) {
    if (FAILED(status)) {
        // Typiquement MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED : boîtier débranché.
        fail(status);
        return S_OK;
    }
    if (streamFlags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) {
        fail(MF_E_END_OF_STREAM);
        return S_OK;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // « sample » peut être nul (ex. simple notification de changement de flux).
    if (sample != nullptr && !stopping_) {
        // On remplace l'image précédente si l'UI ne l'a pas encore affichée :
        // mieux vaut sauter une image que prendre du retard.
        latestSample_ = sample;   // affectation d'un pointeur brut à un ComPtr => AddRef
        SetEvent(frameEvent_);
    }
    requestNextSample();   // et on enchaîne aussitôt sur l'image suivante
    return S_OK;
}

STDMETHODIMP CaptureSession::OnFlush(DWORD /*streamIndex*/) {
    SetEvent(flushDone_.get());
    return S_OK;
}

STDMETHODIMP CaptureSession::OnEvent(DWORD /*streamIndex*/, IMFMediaEvent* event) {
    // Événements de la source : on ne surveille que les erreurs et le retrait du périphérique.
    MediaEventType type = MEUnknown;
    if (event != nullptr && SUCCEEDED(event->GetType(&type))) {
        if (type == MEError || type == MEVideoCaptureDeviceRemoved) {
            fail(MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED);
        }
    }
    return S_OK;
}

} // namespace hdmi
