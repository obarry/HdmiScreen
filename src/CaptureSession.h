// =============================================================================
// CaptureSession.h — Capture asynchrone des images du boîtier
// =============================================================================
//
// Rôle : démarrer la capture sur un périphérique, recevoir les images sur un
// thread de Media Foundation, et ne garder que la plus récente pour le rendu.
//
// Cette classe est un OBJET COM : Media Foundation nous rappelle via
// l'interface IMFSourceReaderCallback. Implémenter COM à la main est le
// passage le plus « bas niveau » du projet ; il est expliqué pas à pas dans
// docs/API_WINDOWS.md §2.4 et docs/CPP_POUR_DEV_JAVA.md §9.

#pragma once

#include "ComUtils.h"
#include "VideoMode.h"

#include <mfidl.h>
#include <mfreadwrite.h>   // [API] IMFSourceReader, IMFSourceReaderCallback

#include <atomic>   // std::atomic ~ AtomicInteger / AtomicBoolean
#include <mutex>    // std::mutex ~ le verrou derrière « synchronized »

namespace hdmi {

// Format des images LIVRÉES par le lecteur (après décodage éventuel du MJPEG).
struct FrameFormat {
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::Other;   // Nv12 ou Yuy2 en pratique
};

// [C++ vs Java] « final » interdit l'héritage, comme en Java.
// « : public IMFSourceReaderCallback » = « implements IMFSourceReaderCallback » :
// en C++ une interface n'est qu'une classe dont toutes les méthodes sont
// virtuelles pures (« = 0 »).
class CaptureSession final : public IMFSourceReaderCallback {
public:
    // Fabrique : crée la session, configure le boîtier et le lecteur.
    //  - source        : périphérique ouvert (openDevice)
    //  - deviceManager : gestionnaire D3D partagé avec le rendu, pour que le
    //                    décodage MJPEG matériel produise directement des textures GPU
    //  - mode          : mode natif choisi (selectBestMode)
    //  - frameEvent    : événement signalé à chaque nouvelle image (réveille la boucle UI)
    // Lève HrError en cas d'échec.
    //
    // [C++] « static » : appelée sans instance, comme en Java.
    static ComPtr<CaptureSession> create(IMFMediaSource* source,
                                         IMFDXGIDeviceManager* deviceManager,
                                         const VideoMode& mode, HANDLE frameEvent);

    // Demande la première image ; les suivantes s'enchaînent toutes seules.
    void start();

    // Arrête proprement la capture et libère le lecteur (et donc le périphérique).
    // À appeler avant de relâcher la dernière référence.
    void stop();

    // Récupère la dernière image reçue (et la retire). Vide si rien de nouveau.
    // Appelée depuis le thread UI.
    ComPtr<IMFSample> takeLatestSample();

    // Format des images livrées.
    const FrameFormat& frameFormat() const { return frameFormat_; }

    // Erreur survenue pendant la capture (boîtier débranché…), ou S_OK.
    HRESULT error() const { return error_.load(); }

    // ---- IUnknown : la base de toute interface COM --------------------------
    // [API] STDMETHODIMP = « HRESULT __stdcall » : convention d'appel imposée par COM.
    // [C++] « override » : le compilateur vérifie qu'on redéfinit bien une
    // méthode virtuelle de la classe parente (comme @Override, mais obligatoire en pratique).
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ---- IMFSourceReaderCallback : appelées par MF, sur SES threads -------------
    STDMETHODIMP OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags,
                              LONGLONG timestamp, IMFSample* sample) override;
    STDMETHODIMP OnFlush(DWORD streamIndex) override;
    STDMETHODIMP OnEvent(DWORD streamIndex, IMFMediaEvent* event) override;

private:
    // Constructeur et destructeur privés : l'objet ne peut être créé que par
    // create() et détruit que par Release() (quand le compteur tombe à zéro).
    explicit CaptureSession(HANDLE frameEvent);
    ~CaptureSession();

    void configureReader(IMFMediaSource* source, IMFDXGIDeviceManager* deviceManager,
                         const VideoMode& mode);
    void requestNextSample();      // appelée avec mutex_ verrouillé
    void fail(HRESULT hr);

    std::atomic<ULONG> refCount_{1};   // compteur de références COM
    std::atomic<HRESULT> error_{S_OK};

    // [C++ vs Java] Pas de « synchronized » : on protège explicitement les
    // données partagées entre threads par un mutex. Tout ce qui suit est
    // accédé par le thread UI ET par les threads de Media Foundation.
    std::mutex mutex_;
    ComPtr<IMFSourceReader> reader_;   // protégé par mutex_
    ComPtr<IMFSample> latestSample_;   // protégé par mutex_
    bool stopping_ = false;            // protégé par mutex_
    bool started_ = false;             // protégé par mutex_ : une lecture a été demandée

    HANDLE frameEvent_;                // possédé par l'appelant (App)
    UniqueHandle flushDone_;           // signalé par OnFlush
    FrameFormat frameFormat_;          // écrit une fois dans create(), puis lu seulement
};

} // namespace hdmi
