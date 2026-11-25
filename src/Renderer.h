// =============================================================================
// Renderer.h — Affichage des images avec Direct3D 11
// =============================================================================
//
// Direct3D 11 (D3D11) est l'API de Windows pour piloter le GPU. Ce qu'elle est,
// et pourquoi on l'utilise ici, est expliqué dans docs/API_WINDOWS.md §4.
//
// Chaîne de rendu d'une image :
//   image capturée (NV12 ou YUY2, en mémoire GPU ou CPU)
//     -> [si CPU] copie dans une texture GPU
//     -> processeur vidéo D3D11 : conversion YUV -> RGB + mise à l'échelle
//     -> tampon arrière de la swap chain
//     -> Present() : l'image s'affiche à l'écran

#pragma once

#include "CaptureSession.h"
#include "ComUtils.h"
#include "Geometry.h"
#include "ShaderScaler.h"

#include <d3d11.h>     // [API] Direct3D 11
#include <dxgi1_3.h>   // [API] DXGI 1.3 : swap chain avec objet d'attente

#include <memory>

namespace hdmi {

class Renderer {
public:
    // Crée le périphérique D3D11 et la swap chain attachée à la fenêtre.
    explicit Renderer(HWND window);

    // [C++] Ni copiable ni déplaçable : il possède des ressources GPU liées à
    // une fenêtre précise. En Java on ne se poserait pas la question.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Gestionnaire à transmettre à Media Foundation (CaptureSession::create)
    // pour que le décodage matériel écrive dans des textures de NOTRE GPU.
    IMFDXGIDeviceManager* deviceManager() const { return deviceManager_.Get(); }

    // Objet à attendre avant de dessiner : signalé quand la swap chain est
    // prête à recevoir une nouvelle image. Limite la latence à une image.
    HANDLE frameLatencyWaitable() const { return frameLatencyWaitable_.get(); }

    // À appeler quand la fenêtre change de taille (WM_SIZE).
    void resize(uint32_t width, uint32_t height);

    // Réglages d'affichage.
    void setScalingMode(ScalingMode mode) { scaling_ = mode; }
    ScalingMode scalingMode() const { return scaling_; }
    void setFullRange(bool fullRange) { fullRange_ = fullRange; }

    // Filtre d'agrandissement et netteté (shaders) : voir ShaderScaler.
    void setUpscaleFilter(UpscaleFilter filter) { upscale_ = filter; }
    UpscaleFilter upscaleFilter() const { return upscale_; }
    void setSharpness(int level) { sharpness_ = level; }
    int sharpness() const { return sharpness_; }

    // Vrai si les shaders d'agrandissement sont utilisables sur ce GPU.
    bool shadersAvailable() const { return shaderScaler_ != nullptr; }

    // Filtre réellement utilisé pour la dernière image (le processeur vidéo
    // remplace les shaders s'ils sont indisponibles ou si le format n'est pas NV12).
    UpscaleFilter effectiveFilter() const { return effectiveFilter_; }

    // Dessine une image capturée puis l'affiche. Lève HrError en cas d'échec.
    void drawFrame(IMFSample* sample, const FrameFormat& format);

    // Affiche un écran vide (pas de signal).
    void drawNoSignal();

private:
    void createDeviceAndManager();
    void createSwapChain(HWND window);
    void createBackBufferViews();
    void releaseBackBufferViews();
    void ensureVideoProcessor(const FrameFormat& format);
    ComPtr<ID3D11Texture2D> uploadFromSystemMemory(IMFMediaBuffer* buffer, const FrameFormat& format);
    ID3D11Texture2D* shaderReadableTexture(ID3D11Texture2D* texture, UINT subresource,
                                           const FrameFormat& format);
    bool drawWithShaders(ID3D11Texture2D* texture, UINT subresource, const FrameFormat& format);
    void present();

    // --- Direct3D 11 et DXGI ---
    ComPtr<ID3D11Device> device_;                 // le GPU : crée les ressources
    ComPtr<ID3D11DeviceContext> context_;         // la file de commandes : copie, efface…
    ComPtr<IDXGISwapChain2> swapChain_;           // les tampons affichés à l'écran
    UniqueHandle frameLatencyWaitable_;
    ComPtr<ID3D11Texture2D> backBuffer_;          // tampon dans lequel on dessine
    ComPtr<ID3D11RenderTargetView> backBufferRtv_; // « vue » pour l'effacer
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // --- Partage avec Media Foundation ---
    ComPtr<IMFDXGIDeviceManager> deviceManager_;
    UINT resetToken_ = 0;

    // --- Processeur vidéo (bloc matériel de conversion et de mise à l'échelle) ---
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnumerator_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;
    ComPtr<ID3D11VideoProcessorOutputView> vpOutputView_;
    FrameFormat vpInputFormat_;     // format pour lequel le processeur a été créé
    uint32_t vpOutputWidth_ = 0;
    uint32_t vpOutputHeight_ = 0;

    // --- Copie depuis la mémoire CPU (images YUY2/NV12 non décodées par le GPU) ---
    ComPtr<ID3D11Texture2D> stagingTexture_;   // écrite par le CPU
    ComPtr<ID3D11Texture2D> uploadTexture_;    // lue par le processeur vidéo ou les shaders
    bool uploadReadableByShaders_ = false;     // créée avec D3D11_BIND_SHADER_RESOURCE
    FrameFormat uploadFormat_;

    // --- Agrandissement par shaders ---
    std::unique_ptr<ShaderScaler> shaderScaler_;   // nul si indisponible sur ce GPU
    ComPtr<ID3D11Texture2D> shaderInput_;          // copie NV12 lisible par les shaders
    FrameFormat shaderInputFormat_;
    UpscaleFilter upscale_ = UpscaleFilter::Bicubic;
    UpscaleFilter effectiveFilter_ = UpscaleFilter::VideoProcessor;
    int sharpness_ = 1;

    ScalingMode scaling_ = ScalingMode::Fit;
    bool fullRange_ = false;
};

} // namespace hdmi
