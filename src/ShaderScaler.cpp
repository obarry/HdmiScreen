// =============================================================================
// ShaderScaler.cpp — Conversion et agrandissement par shaders Direct3D 11
// =============================================================================

#include "ShaderScaler.h"
#include "Log.h"
#include "ScalingShaders.h"

#include <d3dcompiler.h>   // [API] D3DCompile : compilateur HLSL (d3dcompiler_47.dll, fourni avec Windows)

#include <cfloat>    // FLT_MAX
#include <cstring>   // std::strlen
#include <string>

namespace hdmi {

namespace {

// Copie C++ du « constant buffer » Params déclaré en HLSL. L'ordre et la
// taille des champs doivent correspondre EXACTEMENT : le GPU lit les octets
// tels quels. HLSL range les valeurs par paquets de 4 floats (16 octets).
//
// [C++] alignas(16) : adresse multiple de 16 octets, comme l'exige Direct3D
// pour un constant buffer. static_assert vérifie la taille à la compilation :
// une erreur de mise en page devient une erreur de compilation, pas un bug
// d'affichage mystérieux.
struct alignas(16) ShaderParams {
    float srcSize[2];      // HLSL : float2 srcSize
    float dstOrigin[2];    //        float2 dstOrigin
    float dstSize[2];      //        float2 dstSize
    float sharpness;       //        float  sharpness
    float antiRinging;     //        float  antiRinging
    float yOffset;         //        float  yOffset
    float yScale;          //        float  yScale
    float cScale;          //        float  cScale
    float unused;          //        float  unused
};
static_assert(sizeof(ShaderParams) == 48, "ShaderParams doit correspondre au cbuffer HLSL");

// Compile un point d'entrée du code HLSL (ex. « PSScaleLanczos ») pour un
// profil donné (« ps_5_0 » = pixel shader, modèle 5.0 = Direct3D 11).
// [API] Le résultat est du « bytecode » : un programme binaire que le pilote
// du GPU retraduit ensuite pour sa propre architecture.
ComPtr<ID3DBlob> compile(const char* entryPoint, const char* profile) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(kScalingShadersHlsl, std::strlen(kScalingShadersHlsl),
                                  "ScalingShaders.h", nullptr, nullptr, entryPoint, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                  &code, &errors);
    if (FAILED(hr)) {
        // Le compilateur explique l'erreur (ligne, colonne) dans « errors ».
        if (errors) {
            log::error(widen(static_cast<const char*>(errors->GetBufferPointer())));
        }
        throw HrError(hr, "D3DCompile");
    }
    return code;
}

// Crée une texture RGB intermédiaire, à la fois cible de rendu (on y dessine)
// et ressource de shader (la passe suivante la lit), avec ses deux vues.
void createIntermediate(ID3D11Device* device, uint32_t width, uint32_t height,
                        ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11RenderTargetView>& target,
                        ComPtr<ID3D11ShaderResourceView>& view) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    // 16 bits flottants par composante : pas de perte de précision entre les passes.
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    throwIfFailed(device->CreateTexture2D(&desc, nullptr, &texture), "CreateTexture2D(intermédiaire)");
    throwIfFailed(device->CreateRenderTargetView(texture.Get(), nullptr, &target), "CreateRenderTargetView");
    throwIfFailed(device->CreateShaderResourceView(texture.Get(), nullptr, &view), "CreateShaderResourceView");
}

} // namespace

// -----------------------------------------------------------------------------
// Création
// -----------------------------------------------------------------------------

ShaderScaler::ShaderScaler(ID3D11Device* device) : device_(device) {
    // Vertex shader : commun aux trois passes.
    ComPtr<ID3DBlob> vsCode = compile("VSFullscreen", "vs_5_0");
    throwIfFailed(device_->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(),
                                              nullptr, &vertexShader_),
                  "CreateVertexShader");

    convertShader_ = compilePixelShader("PSConvert");
    sharpenShader_ = compilePixelShader("PSSharpen");
    bicubicShader_ = compilePixelShader("PSScaleBicubic");
    lanczosShader_ = compilePixelShader("PSScaleLanczos");

    // [API] Un « sampler » décrit comment lire une texture entre deux pixels :
    // ici interpolation linéaire, et répétition du dernier pixel au-delà des bords.
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = FLT_MAX;
    throwIfFailed(device_->CreateSamplerState(&sampler, &linearClamp_), "CreateSamplerState");

    // [API] Constant buffer : petit bloc de paramètres lisible par les shaders.
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(ShaderParams);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    throwIfFailed(device_->CreateBuffer(&buffer, nullptr, &constants_), "CreateBuffer(constants)");
}

ComPtr<ID3D11PixelShader> ShaderScaler::compilePixelShader(const char* entryPoint) {
    ComPtr<ID3DBlob> code = compile(entryPoint, "ps_5_0");
    ComPtr<ID3D11PixelShader> shader;
    throwIfFailed(device_->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                             &shader),
                  "CreatePixelShader");
    return shader;
}

void ShaderScaler::ensureIntermediateTextures(uint32_t width, uint32_t height) {
    if (rgbTexture_ && width == intermediateWidth_ && height == intermediateHeight_) {
        return;
    }
    createIntermediate(device_.Get(), width, height, rgbTexture_, rgbTarget_, rgbView_);
    createIntermediate(device_.Get(), width, height, sharpTexture_, sharpTarget_, sharpView_);
    intermediateWidth_ = width;
    intermediateHeight_ = height;
}

void ShaderScaler::ensureInputViews(ID3D11Texture2D* nv12) {
    if (inputTexture_.Get() == nv12) {
        return;   // mêmes vues que pour l'image précédente
    }
    // [API] Une texture NV12 contient deux « plans ». On crée une vue par plan,
    // chacune avec le format qui convient : R8 pour Y (1 octet par pixel),
    // R8G8 pour UV (2 octets par pixel, à demi-résolution). Le format choisi
    // pour la vue indique au GPU quel plan on veut lire.
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;

    desc.Format = DXGI_FORMAT_R8_UNORM;
    throwIfFailed(device_->CreateShaderResourceView(nv12, &desc, &planeY_), "CreateShaderResourceView(Y)");
    desc.Format = DXGI_FORMAT_R8G8_UNORM;
    throwIfFailed(device_->CreateShaderResourceView(nv12, &desc, &planeUV_), "CreateShaderResourceView(UV)");
    inputTexture_ = nv12;   // garde la texture en vie tant que ses vues existent
}

// -----------------------------------------------------------------------------
// Dessin
// -----------------------------------------------------------------------------

void ShaderScaler::draw(ID3D11DeviceContext* context, const Frame& frame, const Target& target,
                        const Settings& settings) {
    ensureInputViews(frame.nv12);
    ensureIntermediateTextures(frame.width, frame.height);

    // 1. Paramètres de l'image, envoyés au GPU.
    const Rect& dst = target.destination;
    ShaderParams params{};
    params.srcSize[0] = static_cast<float>(frame.width);
    params.srcSize[1] = static_cast<float>(frame.height);
    params.dstOrigin[0] = static_cast<float>(dst.left);
    params.dstOrigin[1] = static_cast<float>(dst.top);
    params.dstSize[0] = static_cast<float>(dst.width());
    params.dstSize[1] = static_cast<float>(dst.height());
    // Niveau 1 -> 0.0 (doux), 2 -> 0.5, 3 -> 1.0 (fort) ; 0 = passe B sautée.
    params.sharpness = settings.sharpness <= 1 ? 0.0f
                                               : static_cast<float>(settings.sharpness - 1) /
                                                     static_cast<float>(kMaxSharpnessLevel - 1);
    params.antiRinging = 0.5f;
    if (settings.fullRange) {
        params.yOffset = 0.0f;
        params.yScale = 1.0f;
        params.cScale = 1.0f;
    } else {
        // Plage limitée : noir = 16, blanc = 235, chrominance entre 16 et 240.
        params.yOffset = 16.0f / 255.0f;
        params.yScale = 255.0f / 219.0f;
        params.cScale = 255.0f / 224.0f;
    }
    // [API] UpdateSubresource copie nos octets dans le constant buffer du GPU.
    context->UpdateSubresource(constants_.Get(), 0, nullptr, &params, 0, 0);

    // 2. États communs aux trois passes.
    //    Pas de tampon de sommets : le vertex shader calcule ses 3 sommets lui-même.
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->RSSetState(nullptr);                      // états par défaut
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    ID3D11Buffer* constants = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &constants);
    ID3D11SamplerState* sampler = linearClamp_.Get();
    context->PSSetSamplers(0, 1, &sampler);

    // [API] Le « viewport » fixe la zone de la cible où le triangle est dessiné.
    // [C++] Lambda locale : évite de répéter les 6 champs à chaque passe.
    auto setViewport = [context](float x, float y, float width, float height) {
        const D3D11_VIEWPORT viewport{x, y, width, height, 0.0f, 1.0f};
        context->RSSetViewports(1, &viewport);
    };
    // Une texture ne peut pas être lue et écrite en même temps : avant de
    // changer de cible, on délie toutes les textures lues (tableau de nullptr).
    ID3D11ShaderResourceView* const none[3] = {nullptr, nullptr, nullptr};
    const auto width = static_cast<float>(frame.width);
    const auto height = static_cast<float>(frame.height);

    // 3. Passe A : NV12 -> RGB, dans rgbTexture_.
    context->PSSetShaderResources(0, 3, none);
    ID3D11RenderTargetView* renderTarget = rgbTarget_.Get();
    context->OMSetRenderTargets(1, &renderTarget, nullptr);
    setViewport(0.0f, 0.0f, width, height);
    ID3D11ShaderResourceView* const planes[2] = {planeY_.Get(), planeUV_.Get()};
    context->PSSetShaderResources(0, 2, planes);
    context->PSSetShader(convertShader_.Get(), nullptr, 0);
    context->Draw(3, 0);   // 3 sommets = 1 triangle couvrant toute la cible

    // 4. Passe B (optionnelle) : renforcement de netteté, dans sharpTexture_.
    ID3D11ShaderResourceView* source = rgbView_.Get();
    if (settings.sharpness > 0) {
        context->PSSetShaderResources(0, 3, none);
        renderTarget = sharpTarget_.Get();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        context->PSSetShaderResources(2, 1, &source);
        context->PSSetShader(sharpenShader_.Get(), nullptr, 0);
        context->Draw(3, 0);
        source = sharpView_.Get();
    }

    // 5. Passe C : agrandissement vers le tampon arrière, dans le rectangle de destination.
    context->PSSetShaderResources(0, 3, none);
    renderTarget = target.view;
    context->OMSetRenderTargets(1, &renderTarget, nullptr);
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(target.view, black);   // bandes noires
    setViewport(static_cast<float>(dst.left), static_cast<float>(dst.top),
                static_cast<float>(dst.width()), static_cast<float>(dst.height()));
    context->PSSetShaderResources(2, 1, &source);
    context->PSSetShader(settings.filter == UpscaleFilter::Lanczos ? lanczosShader_.Get()
                                                                   : bicubicShader_.Get(),
                         nullptr, 0);
    context->Draw(3, 0);

    // 6. Tout délier : les textures seront réécrites à l'image suivante.
    context->PSSetShaderResources(0, 3, none);
    context->OMSetRenderTargets(0, nullptr, nullptr);
}

} // namespace hdmi
