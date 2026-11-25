// =============================================================================
// Renderer.cpp — Affichage des images avec Direct3D 11
// =============================================================================

#include "Renderer.h"
#include "Log.h"

#include <d3d10.h>     // [API] ID3D10Multithread (protection multithread du périphérique)
#include <mfapi.h>     // [API] MFCreateDXGIDeviceManager

#include <cstring>     // std::memcpy
#include <iterator>    // std::size

namespace hdmi {

namespace {

DXGI_FORMAT toDxgiFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::Nv12: return DXGI_FORMAT_NV12;
        case PixelFormat::Yuy2: return DXGI_FORMAT_YUY2;
        default:                return DXGI_FORMAT_UNKNOWN;
    }
}

bool sameFormat(const FrameFormat& a, const FrameFormat& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format;
}

// Verrouille un tampon Media Foundation pour lire ses octets côté CPU, et le
// déverrouille automatiquement à la sortie de portée (RAII).
//
// [API] Deux cas :
//  - IMF2DBuffer (tampon « 2D ») : Lock2D donne le début de l'image et le
//    « pitch », c'est-à-dire le nombre d'octets entre deux lignes (souvent
//    plus que la largeur utile, pour aligner les lignes en mémoire) ;
//  - IMFMediaBuffer simple : Lock donne un bloc d'octets ; le pitch vaut alors
//    la largeur utile d'une ligne.
class LockedBuffer {
public:
    LockedBuffer(IMFMediaBuffer* buffer, LONG defaultPitch) {
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d_)))) {
            throwIfFailed(buffer2d_->Lock2D(&data_, &pitch_), "IMF2DBuffer::Lock2D");
        } else {
            DWORD length = 0;
            throwIfFailed(buffer->Lock(&data_, nullptr, &length), "IMFMediaBuffer::Lock");
            buffer_ = buffer;   // seulement une fois le verrou obtenu
            length_ = length;
            pitch_ = defaultPitch;
        }
    }
    ~LockedBuffer() {
        if (buffer2d_) {
            buffer2d_->Unlock2D();
        } else if (buffer_) {
            buffer_->Unlock();
        }
    }
    LockedBuffer(const LockedBuffer&) = delete;
    LockedBuffer& operator=(const LockedBuffer&) = delete;

    const BYTE* data() const { return data_; }
    LONG pitch() const { return pitch_; }
    // Taille connue seulement pour un tampon simple ; 0 = inconnue (tampon 2D).
    DWORD length() const { return length_; }

private:
    ComPtr<IMF2DBuffer> buffer2d_;
    ComPtr<IMFMediaBuffer> buffer_;
    BYTE* data_ = nullptr;   // [C++] pointeur brut NON propriétaire : on ne le libère pas
    LONG pitch_ = 0;
    DWORD length_ = 0;
};

// Copie un « plan » d'image ligne par ligne, chaque côté ayant son propre pitch.
// [C++] Arithmétique de pointeurs : « src + n » avance de n octets car src
// pointe sur des BYTE. Aucun contrôle de bornes : c'est à nous d'être exacts.
void copyPlane(BYTE* dst, UINT dstPitch, const BYTE* src, LONG srcPitch, UINT rowBytes, UINT rows) {
    for (UINT row = 0; row < rows; ++row) {
        std::memcpy(dst + static_cast<size_t>(row) * dstPitch,
                    src + static_cast<size_t>(row) * srcPitch, rowBytes);
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Création
// -----------------------------------------------------------------------------

Renderer::Renderer(HWND window) {
    createDeviceAndManager();
    createSwapChain(window);

    // Les shaders sont un plus : s'ils ne peuvent pas être préparés (GPU trop
    // ancien, compilateur HLSL absent), on garde le processeur vidéo.
    try {
        shaderScaler_ = std::make_unique<ShaderScaler>(device_.Get());
    } catch (const HrError& e) {
        log::error(L"Agrandissement par shaders indisponible, processeur vidéo utilisé : " +
                   widen(e.what()));
    }
}

void Renderer::createDeviceAndManager() {
    // [API] Options du périphérique :
    //  - BGRA_SUPPORT  : format de couleur standard de l'affichage Windows ;
    //  - VIDEO_SUPPORT : indispensable pour le processeur vidéo et pour le
    //                    partage avec Media Foundation.
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

    // [API] « Feature levels » : niveaux de capacité du GPU qu'on accepte.
    // 11_0 correspond à tout GPU des dix dernières années.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};

    // [API] nullptr = adaptateur par défaut ; HARDWARE = vrai GPU (pas l'émulation logicielle).
    // [C++] std::size(levels) : nombre d'éléments d'un tableau C, connu à la compilation.
    throwIfFailed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                    static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                    &device_, nullptr, &context_),
                  "D3D11CreateDevice");

    // [API] Le contexte sera utilisé par deux threads (le nôtre et ceux de MF
    // pour le décodage) : on active son verrou interne.
    // [C++] ComPtr::As = QueryInterface typé : « demande-lui l'interface X ».
    ComPtr<ID3D10Multithread> multithread;
    throwIfFailed(context_.As(&multithread), "QueryInterface(ID3D10Multithread)");
    multithread->SetMultithreadProtected(TRUE);

    // [API] Gestionnaire qui prête notre périphérique à Media Foundation.
    throwIfFailed(MFCreateDXGIDeviceManager(&resetToken_, &deviceManager_), "MFCreateDXGIDeviceManager");
    throwIfFailed(deviceManager_->ResetDevice(device_.Get(), resetToken_),
                  "IMFDXGIDeviceManager::ResetDevice");

    // [API] Interfaces « vidéo » du même périphérique et du même contexte.
    throwIfFailed(device_.As(&videoDevice_), "QueryInterface(ID3D11VideoDevice)");
    throwIfFailed(context_.As(&videoContext_), "QueryInterface(ID3D11VideoContext)");
}

void Renderer::createSwapChain(HWND window) {
    // [API] On remonte du périphérique D3D à la « fabrique » DXGI qui l'a créé :
    // périphérique -> adaptateur (la carte graphique) -> fabrique.
    ComPtr<IDXGIDevice> dxgiDevice;
    throwIfFailed(device_.As(&dxgiDevice), "QueryInterface(IDXGIDevice)");
    ComPtr<IDXGIAdapter> adapter;
    throwIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
    ComPtr<IDXGIFactory2> factory;
    // [API] IID_PPV_ARGS(&x) fournit à la fois l'identifiant de l'interface
    // voulue et l'adresse où la déposer : évite de se tromper de type.
    throwIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIAdapter::GetParent");

    // [API] Description de la swap chain (voir docs/API_WINDOWS.md §4.4).
    DXGI_SWAP_CHAIN_DESC1 desc{};                  // {} : tous les champs à zéro
    desc.Width = 0;                                // 0 = taille actuelle de la fenêtre
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;      // 8 bits par composante, ordre BGRA
    desc.SampleDesc.Count = 1;                     // pas d'anticrénelage
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;                          // un tampon affiché, un en préparation
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;   // modèle « flip » : le plus efficace
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain1;
    throwIfFailed(factory->CreateSwapChainForHwnd(device_.Get(), window, &desc, nullptr, nullptr,
                                                  &swapChain1),
                  "IDXGIFactory2::CreateSwapChainForHwnd");

    // Désactive Alt+Entrée (plein écran « exclusif » de DXGI) : on gère nous-mêmes
    // le plein écran avec une fenêtre sans bordure, plus souple et aussi rapide.
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    throwIfFailed(swapChain1.As(&swapChain_), "QueryInterface(IDXGISwapChain2)");
    // Au plus une image en attente d'affichage : latence minimale.
    throwIfFailed(swapChain_->SetMaximumFrameLatency(1), "IDXGISwapChain2::SetMaximumFrameLatency");
    frameLatencyWaitable_.reset(swapChain_->GetFrameLatencyWaitableObject());

    DXGI_SWAP_CHAIN_DESC1 actual{};
    throwIfFailed(swapChain_->GetDesc1(&actual), "IDXGISwapChain1::GetDesc1");
    width_ = actual.Width;
    height_ = actual.Height;
    createBackBufferViews();
}

void Renderer::createBackBufferViews() {
    // [API] Avec le modèle flip en D3D11, le tampon 0 désigne toujours le
    // tampon ARRIÈRE courant : on peut garder cette référence d'une image à l'autre.
    throwIfFailed(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer_)), "IDXGISwapChain::GetBuffer");
    // [API] Une « vue » (view) dit comment une opération voit une ressource ;
    // une RenderTargetView permet d'écrire dedans (ici : l'effacer).
    throwIfFailed(device_->CreateRenderTargetView(backBuffer_.Get(), nullptr, &backBufferRtv_),
                  "ID3D11Device::CreateRenderTargetView");
}

void Renderer::releaseBackBufferViews() {
    // Toutes les références au tampon arrière doivent disparaître avant
    // ResizeBuffers, sinon l'appel échoue.
    vpOutputView_.Reset();
    backBufferRtv_.Reset();
    backBuffer_.Reset();
    context_->ClearState();
}

void Renderer::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == width_ && height == height_)) {
        return;   // fenêtre réduite, ou taille inchangée
    }
    releaseBackBufferViews();
    // Les drapeaux doivent rester ceux de la création.
    throwIfFailed(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT),
                  "IDXGISwapChain::ResizeBuffers");
    width_ = width;
    height_ = height;
    createBackBufferViews();
    vpOutputWidth_ = 0;   // force la recréation du processeur vidéo (taille de sortie changée)
}

// -----------------------------------------------------------------------------
// Processeur vidéo
// -----------------------------------------------------------------------------

void Renderer::ensureVideoProcessor(const FrameFormat& format) {
    if (videoProcessor_ && vpOutputView_ && sameFormat(format, vpInputFormat_) &&
        vpOutputWidth_ == width_ && vpOutputHeight_ == height_) {
        return;   // déjà prêt pour ce format et cette taille
    }

    // [API] On décrit le travail attendu ; le pilote choisit son implémentation.
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;   // pas d'entrelacement
    content.InputFrameRate = {60, 1};
    content.InputWidth = format.width;
    content.InputHeight = format.height;
    content.OutputFrameRate = {60, 1};
    content.OutputWidth = width_;
    content.OutputHeight = height_;
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;   // privilégier la netteté de l'agrandissement

    vpOutputView_.Reset();
    videoProcessor_.Reset();
    vpEnumerator_.Reset();
    throwIfFailed(videoDevice_->CreateVideoProcessorEnumerator(&content, &vpEnumerator_),
                  "ID3D11VideoDevice::CreateVideoProcessorEnumerator");

    // Vérifie que le GPU accepte ce format en entrée.
    UINT support = 0;
    throwIfFailed(vpEnumerator_->CheckVideoProcessorFormat(toDxgiFormat(format.format), &support),
                  "CheckVideoProcessorFormat");
    if ((support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0) {
        throw HrError(E_NOTIMPL, "Format d'entrée non pris en charge par le processeur vidéo du GPU");
    }

    throwIfFailed(videoDevice_->CreateVideoProcessor(vpEnumerator_.Get(), 0, &videoProcessor_),
                  "ID3D11VideoDevice::CreateVideoProcessor");

    // La sortie du processeur vidéo est notre tampon arrière.
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;
    throwIfFailed(videoDevice_->CreateVideoProcessorOutputView(backBuffer_.Get(), vpEnumerator_.Get(),
                                                               &outputDesc, &vpOutputView_),
                  "ID3D11VideoDevice::CreateVideoProcessorOutputView");

    // Réglages fixes : image progressive, pas de « traitements automatiques »
    // (débruitage, amélioration des contours…) qui ajouteraient de la latence.
    videoContext_->VideoProcessorSetStreamFrameFormat(videoProcessor_.Get(), 0,
                                                      D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(videoProcessor_.Get(), 0, FALSE);

    vpInputFormat_ = format;
    vpOutputWidth_ = width_;
    vpOutputHeight_ = height_;
}

// -----------------------------------------------------------------------------
// Copie d'une image depuis la mémoire CPU
// -----------------------------------------------------------------------------

ComPtr<ID3D11Texture2D> Renderer::uploadFromSystemMemory(IMFMediaBuffer* buffer,
                                                         const FrameFormat& format) {
    const DXGI_FORMAT dxgiFormat = toDxgiFormat(format.format);
    if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
        throw HrError(E_NOTIMPL, "Format d'image non pris en charge");
    }

    // (Re)créer les deux textures si le format a changé.
    //  - « staging » : accessible en écriture par le CPU (Map), invisible du GPU ;
    //  - « upload »  : mémoire GPU, lisible par le processeur vidéo.
    // [API] On ne peut pas faire les deux avec une seule texture : c'est le
    // pilote qui décide où vit la mémoire selon l'usage déclaré.
    if (!stagingTexture_ || !sameFormat(format, uploadFormat_)) {
        stagingTexture_.Reset();
        uploadTexture_.Reset();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = format.width;
        desc.Height = format.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = dxgiFormat;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = 0;
        throwIfFailed(device_->CreateTexture2D(&desc, nullptr, &stagingTexture_),
                      "CreateTexture2D(staging)");

        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        uploadReadableByShaders_ = true;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &uploadTexture_))) {
            // Certains pilotes refusent ce drapeau pour YUY2 : on réessaie sans.
            // Les shaders ne pourront alors pas la lire (processeur vidéo seulement).
            desc.BindFlags = 0;
            uploadReadableByShaders_ = false;
            throwIfFailed(device_->CreateTexture2D(&desc, nullptr, &uploadTexture_),
                          "CreateTexture2D(upload)");
        }
        uploadFormat_ = format;
    }

    // Octets utiles par ligne : 2 par pixel en YUY2, 1 par pixel pour le plan Y du NV12.
    const UINT rowBytes = format.format == PixelFormat::Yuy2 ? format.width * 2 : format.width;
    const LockedBuffer source(buffer, static_cast<LONG>(rowBytes));
    if (source.pitch() < static_cast<LONG>(rowBytes)) {
        throw HrError(E_UNEXPECTED, "Image mal formée (pitch trop petit ou image inversée)");
    }
    // Pour un tampon simple, on vérifie qu'il contient bien toute l'image.
    const size_t rowsTotal = format.format == PixelFormat::Nv12 ? format.height * 3 / 2 : format.height;
    if (source.length() != 0 && source.length() < static_cast<size_t>(source.pitch()) * rowsTotal) {
        throw HrError(E_UNEXPECTED, "Image incomplète");
    }

    // [API] Map donne un pointeur CPU vers la texture staging, avec SON pitch.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    throwIfFailed(context_->Map(stagingTexture_.Get(), 0, D3D11_MAP_WRITE, 0, &mapped),
                  "ID3D11DeviceContext::Map");
    BYTE* destination = static_cast<BYTE*>(mapped.pData);

    // Plan Y (NV12) ou image entière (YUY2).
    copyPlane(destination, mapped.RowPitch, source.data(), source.pitch(), rowBytes, format.height);
    if (format.format == PixelFormat::Nv12) {
        // NV12 : le plan UV (demi-hauteur, même largeur en octets) suit le plan Y,
        // aussi bien dans le tampon MF que dans la texture mappée.
        copyPlane(destination + static_cast<size_t>(mapped.RowPitch) * format.height, mapped.RowPitch,
                  source.data() + static_cast<size_t>(source.pitch()) * format.height, source.pitch(),
                  rowBytes, format.height / 2);
    }
    context_->Unmap(stagingTexture_.Get(), 0);

    // Copie GPU staging -> upload (rapide, faite par le GPU).
    context_->CopyResource(uploadTexture_.Get(), stagingTexture_.Get());
    return uploadTexture_;
}

// -----------------------------------------------------------------------------
// Dessin
// -----------------------------------------------------------------------------

void Renderer::drawFrame(IMFSample* sample, const FrameFormat& format) {
    if (!backBuffer_) {
        return;
    }

    ComPtr<IMFMediaBuffer> buffer;
    throwIfFailed(sample->GetBufferByIndex(0, &buffer), "IMFSample::GetBufferByIndex");

    // 1. Obtenir l'image sous forme de texture GPU.
    ComPtr<ID3D11Texture2D> texture;
    UINT subresource = 0;
    ComPtr<IMFDXGIBuffer> dxgiBuffer;
    if (SUCCEEDED(buffer.As(&dxgiBuffer))) {
        // Cas « GPU » : le décodeur matériel a déjà écrit dans une texture.
        // Elle fait souvent partie d'un TABLEAU de textures : « subresource »
        // indique laquelle.
        throwIfFailed(dxgiBuffer->GetResource(IID_PPV_ARGS(&texture)), "IMFDXGIBuffer::GetResource");
        throwIfFailed(dxgiBuffer->GetSubresourceIndex(&subresource), "IMFDXGIBuffer::GetSubresourceIndex");
    } else {
        // Cas « CPU » : image non compressée livrée en mémoire système.
        texture = uploadFromSystemMemory(buffer.Get(), format);
    }

    // 2. Agrandissement par shaders, si choisi et possible.
    if (drawWithShaders(texture.Get(), subresource, format)) {
        effectiveFilter_ = upscale_;
        present();
        return;
    }
    effectiveFilter_ = UpscaleFilter::VideoProcessor;

    // 3. Sinon : processeur vidéo. Le préparer.
    ensureVideoProcessor(format);

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
    inputDesc.FourCC = 0;   // 0 = déduit du format de la texture
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDesc.Texture2D.MipSlice = 0;
    inputDesc.Texture2D.ArraySlice = subresource;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    throwIfFailed(videoDevice_->CreateVideoProcessorInputView(texture.Get(), vpEnumerator_.Get(),
                                                              &inputDesc, &inputView),
                  "ID3D11VideoDevice::CreateVideoProcessorInputView");

    // 4. Rectangles : toute l'image source -> rectangle calculé (bandes noires).
    const Rect dst = computeDestinationRect(static_cast<int32_t>(format.width),
                                            static_cast<int32_t>(format.height),
                                            static_cast<int32_t>(width_),
                                            static_cast<int32_t>(height_), scaling_);
    const RECT sourceRect{0, 0, static_cast<LONG>(format.width), static_cast<LONG>(format.height)};
    const RECT destRect{dst.left, dst.top, dst.right, dst.bottom};
    const RECT targetRect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};

    ID3D11VideoProcessor* vp = videoProcessor_.Get();
    // [C++] const_cast : ces anciennes API attendent un pointeur NON const
    // alors qu'elles ne modifient pas le rectangle. On retire le const pour elles.
    videoContext_->VideoProcessorSetStreamSourceRect(vp, 0, TRUE, const_cast<RECT*>(&sourceRect));
    videoContext_->VideoProcessorSetStreamDestRect(vp, 0, TRUE, const_cast<RECT*>(&destRect));
    videoContext_->VideoProcessorSetOutputTargetRect(vp, TRUE, const_cast<RECT*>(&targetRect));

    // Couleur des bandes : noir. (Hors du rectangle de destination, le
    // processeur vidéo remplit avec cette couleur.)
    D3D11_VIDEO_COLOR black{};
    black.RGBA.A = 1.0f;
    videoContext_->VideoProcessorSetOutputBackgroundColor(vp, FALSE, &black);

    // 5. Espaces de couleur (docs/API_WINDOWS.md §5) :
    //    entrée = YUV BT.709 (norme HD), plage 16-235 sauf --full-range ;
    //    sortie = RGB pleine plage 0-255 (celle de l'écran).
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputSpace{};
    inputSpace.YCbCr_Matrix = 1;   // 1 = BT.709, 0 = BT.601
    inputSpace.Nominal_Range = fullRange_ ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
                                          : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    videoContext_->VideoProcessorSetStreamColorSpace(vp, 0, &inputSpace);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputSpace{};
    outputSpace.RGB_Range = 0;     // 0 = 0-255
    videoContext_->VideoProcessorSetOutputColorSpace(vp, &outputSpace);

    // 6. Conversion + mise à l'échelle en un seul appel, exécuté par le GPU.
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    throwIfFailed(videoContext_->VideoProcessorBlt(vp, vpOutputView_.Get(), 0, 1, &stream),
                  "ID3D11VideoContext::VideoProcessorBlt");

    present();
}

// Renvoie une texture NV12 que les shaders peuvent lire, ou nullptr.
//  - Image copiée depuis la mémoire CPU : uploadTexture_ convient directement.
//  - Image décodée par le GPU (MJPEG) : la texture du décodeur n'est souvent
//    pas lisible par les shaders, et fait partie d'un tableau. On copie la
//    bonne tranche dans notre propre texture (copie GPU -> GPU, très rapide).
ID3D11Texture2D* Renderer::shaderReadableTexture(ID3D11Texture2D* texture, UINT subresource,
                                                 const FrameFormat& format) {
    if (texture == uploadTexture_.Get()) {
        return uploadReadableByShaders_ ? texture : nullptr;
    }
    if (!shaderInput_ || !sameFormat(format, shaderInputFormat_)) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = format.width;
        desc.Height = format.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        shaderInput_.Reset();
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &shaderInput_))) {
            return nullptr;
        }
        shaderInputFormat_ = format;
    }
    // [API] Les surfaces des décodeurs sont souvent plus hautes que l'image
    // (ex. 1088 lignes pour 1080) : on ne copie que la zone utile (« box »).
    const D3D11_BOX box{0, 0, 0, format.width, format.height, 1};
    context_->CopySubresourceRegion(shaderInput_.Get(), 0, 0, 0, 0, texture, subresource, &box);
    return shaderInput_.Get();
}

// Dessine l'image avec les shaders. Renvoie false s'il faut utiliser le
// processeur vidéo à la place (choix de l'utilisateur, format, ou erreur).
bool Renderer::drawWithShaders(ID3D11Texture2D* texture, UINT subresource, const FrameFormat& format) {
    if (upscale_ == UpscaleFilter::VideoProcessor || !shaderScaler_ ||
        format.format != PixelFormat::Nv12) {
        return false;
    }
    ID3D11Texture2D* input = shaderReadableTexture(texture, subresource, format);
    if (input == nullptr) {
        return false;
    }

    const Rect dst = computeDestinationRect(static_cast<int32_t>(format.width),
                                            static_cast<int32_t>(format.height),
                                            static_cast<int32_t>(width_),
                                            static_cast<int32_t>(height_), scaling_);
    try {
        shaderScaler_->draw(context_.Get(), {input, format.width, format.height},
                            {backBufferRtv_.Get(), width_, height_, dst},
                            {upscale_, sharpness_, fullRange_});
    } catch (const HrError& e) {
        // Erreur inattendue (mémoire GPU…) : on abandonne les shaders pour la
        // suite de la session plutôt que d'échouer à chaque image.
        log::error(L"Agrandissement par shaders désactivé : " + widen(e.what()));
        shaderScaler_.reset();
        return false;
    }
    return true;
}

void Renderer::drawNoSignal() {
    if (!backBufferRtv_) {
        return;
    }
    // Gris très sombre : distingue « pas de signal » d'un écran noir capturé.
    const float color[4] = {0.06f, 0.06f, 0.08f, 1.0f};
    context_->ClearRenderTargetView(backBufferRtv_.Get(), color);
    present();
}

void Renderer::present() {
    // [API] Present(1, 0) : affiche au prochain rafraîchissement de l'écran
    // (synchronisation verticale), sans déchirure de l'image.
    throwIfFailed(swapChain_->Present(1, 0), "IDXGISwapChain::Present");
}

} // namespace hdmi
