// =============================================================================
// ShaderScaler.h — Conversion et agrandissement par shaders Direct3D 11
// =============================================================================
//
// Alternative au processeur vidéo du GPU (Renderer::drawFrame) : on DESSINE
// nous-mêmes l'image agrandie avec nos propres shaders (ScalingShaders.h), ce
// qui permet des filtres plus nets (bicubique, Lanczos) et un renforcement de
// netteté réglable.
//
// C'est le « pipeline graphique » de Direct3D, celui des jeux : vertex shader,
// pixel shader, cible de rendu, viewport. Il est expliqué dans
// docs/API_WINDOWS.md §4.7, en regard du processeur vidéo (§4.5).
//
// Ne gère que le format NV12 (celui du boîtier à 60 i/s). Pour les autres,
// Renderer garde le processeur vidéo.

#pragma once

#include "ComUtils.h"
#include "Geometry.h"

#include <d3d11.h>

#include <cstdint>

namespace hdmi {

class ShaderScaler {
public:
    // Compile les shaders et crée les objets fixes. Lève HrError si le GPU ou
    // le compilateur HLSL refuse : Renderer revient alors au processeur vidéo.
    explicit ShaderScaler(ID3D11Device* device);

    ShaderScaler(const ShaderScaler&) = delete;
    ShaderScaler& operator=(const ShaderScaler&) = delete;

    // Ce qu'il faut pour dessiner une image.
    struct Frame {
        ID3D11Texture2D* nv12 = nullptr;   // texture NV12 créée avec D3D11_BIND_SHADER_RESOURCE
        uint32_t width = 0;                // taille de l'image source
        uint32_t height = 0;
    };
    struct Target {
        ID3D11RenderTargetView* view = nullptr;   // tampon arrière de la swap chain
        uint32_t width = 0;
        uint32_t height = 0;
        Rect destination;                          // où placer l'image (bandes noires autour)
    };
    struct Settings {
        UpscaleFilter filter = UpscaleFilter::Bicubic;   // Bicubic ou Lanczos
        int sharpness = 1;                                // 0 à kMaxSharpnessLevel
        bool fullRange = false;
    };

    // Exécute les trois passes (conversion, netteté, agrandissement).
    // N'appelle pas Present : c'est le rôle de Renderer.
    void draw(ID3D11DeviceContext* context, const Frame& frame, const Target& target,
              const Settings& settings);

private:
    ComPtr<ID3D11PixelShader> compilePixelShader(const char* entryPoint);
    void ensureIntermediateTextures(uint32_t width, uint32_t height);
    void ensureInputViews(ID3D11Texture2D* nv12);

    ComPtr<ID3D11Device> device_;

    // Shaders et états fixes
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> convertShader_;
    ComPtr<ID3D11PixelShader> sharpenShader_;
    ComPtr<ID3D11PixelShader> bicubicShader_;
    ComPtr<ID3D11PixelShader> lanczosShader_;
    ComPtr<ID3D11SamplerState> linearClamp_;
    ComPtr<ID3D11Buffer> constants_;

    // Vues sur la texture NV12 d'entrée (plan Y et plan UV), recréées si la texture change.
    ComPtr<ID3D11Texture2D> inputTexture_;
    ComPtr<ID3D11ShaderResourceView> planeY_;
    ComPtr<ID3D11ShaderResourceView> planeUV_;

    // Images RGB intermédiaires, à la taille de la source.
    uint32_t intermediateWidth_ = 0;
    uint32_t intermediateHeight_ = 0;
    ComPtr<ID3D11Texture2D> rgbTexture_;
    ComPtr<ID3D11RenderTargetView> rgbTarget_;
    ComPtr<ID3D11ShaderResourceView> rgbView_;
    ComPtr<ID3D11Texture2D> sharpTexture_;
    ComPtr<ID3D11RenderTargetView> sharpTarget_;
    ComPtr<ID3D11ShaderResourceView> sharpView_;
};

} // namespace hdmi
