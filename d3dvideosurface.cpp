#include "d3dvideosurface.h"
#include "d3d11videodevice.h"

#include <QDebug>
#include <QResizeEvent>

#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace {

struct Vertex
{
    float x;
    float y;
    float u;
    float v;
};

struct OverlayVertex
{
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

struct OverlayColor
{
    float r;
    float g;
    float b;
    float a;
};

QString hresultText(HRESULT hr)
{
    return QStringLiteral("HRESULT 0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)),
                                              8,
                                              16,
                                              QLatin1Char('0'));
}

const char *kVideoShader = R"HLSL(
struct VSIn {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PSIn {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSIn vsMain(VSIn input) {
    PSIn output;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

Texture2D yTexture : register(t0);
Texture2D uvTexture : register(t1);
SamplerState videoSampler : register(s0);

float4 psMain(PSIn input) : SV_TARGET {
    float y = yTexture.Sample(videoSampler, input.uv).r;
    float2 uv = uvTexture.Sample(videoSampler, input.uv).rg;
    float Y = 1.164383 * (y - 0.0625);
    float U = uv.x - 0.5;
    float V = uv.y - 0.5;
    float3 rgb;
    rgb.r = Y + 1.596027 * V;
    rgb.g = Y - 0.391762 * U - 0.812968 * V;
    rgb.b = Y + 2.017232 * U;
    return float4(saturate(rgb), 1.0);
}
)HLSL";

const char *kOverlayShader = R"HLSL(
struct VSIn {
    float2 pos : POSITION;
    float4 color : COLOR0;
};

struct PSIn {
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};

PSIn vsMain(VSIn input) {
    PSIn output;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.color = input.color;
    return output;
}

float4 psMain(PSIn input) : SV_TARGET {
    return input.color;
}
)HLSL";

OverlayColor colorForInstance(const AthleteInstance &instance)
{
    if (instance.identityStatus == QStringLiteral("identified")) {
        return {0.10f, 0.86f, 0.54f, 0.95f};
    }
    if (instance.identityStatus == QStringLiteral("ambiguous")) {
        return {1.0f, 0.74f, 0.22f, 0.95f};
    }
    return {0.44f, 0.80f, 1.0f, 0.95f};
}

bool mapLandmarkToNdc(const QPointF &point,
                      const QSizeF &sourceSize,
                      float u0,
                      float u1,
                      float v0,
                      float v1,
                      float *x,
                      float *y)
{
    if (sourceSize.width() <= 0.0 || sourceSize.height() <= 0.0 || u1 <= u0 || v1 <= v0) {
        return false;
    }

    const float u = static_cast<float>(point.x() / sourceSize.width());
    const float v = static_cast<float>(point.y() / sourceSize.height());
    if (u < u0 || u > u1 || v < v0 || v > v1) {
        return false;
    }

    *x = ((u - u0) / (u1 - u0)) * 2.0f - 1.0f;
    *y = 1.0f - ((v - v0) / (v1 - v0)) * 2.0f;
    return true;
}

void appendLine(std::vector<OverlayVertex> *vertices,
                float x0,
                float y0,
                float x1,
                float y1,
                OverlayColor color)
{
    vertices->push_back({x0, y0, color.r, color.g, color.b, color.a});
    vertices->push_back({x1, y1, color.r, color.g, color.b, color.a});
}

void appendBox(std::vector<OverlayVertex> *vertices,
               const std::array<std::pair<float, float>, 4> &mappedBox,
               OverlayColor color)
{
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        const auto [x0, y0] = mappedBox[static_cast<size_t>(i)];
        const auto [x1, y1] = mappedBox[static_cast<size_t>(next)];
        appendLine(vertices, x0, y0, x1, y1, color);
    }
}

} // namespace

D3DVideoSurface::D3DVideoSurface(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
}

D3DVideoSurface::~D3DVideoSurface() = default;

void D3DVideoSurface::presentFrame(const std::shared_ptr<D3DFrame> &frame)
{
    if (!frame || !frame->texture) {
        return;
    }
    if (!ensureRenderResources() || !ensureCopyTexture(*frame) || !uploadFrame(*frame)) {
        return;
    }
    render(*frame);
}

void D3DVideoSurface::clearFrame()
{
    if (!ensureSwapChain() || !m_renderTargetView) {
        return;
    }
    constexpr float clearColor[] = {0.078f, 0.09f, 0.114f, 1.0f};
    auto *context = D3D11VideoDevice::context();
    context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
    m_swapChain->Present(0, 0);
}

void D3DVideoSurface::setAthleteFrame(const AthleteFrameResult &frame)
{
    m_athleteFrame = frame;
}

void D3DVideoSurface::setPoseFrame(const PoseFrameResult &frame)
{
    m_poseFrame = frame;
}

QString D3DVideoSurface::lastError() const
{
    return m_lastError;
}

void D3DVideoSurface::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    releaseSizeDependentResources();
}

void D3DVideoSurface::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    clearFrame();
}

bool D3DVideoSurface::ensureRenderResources()
{
    return ensureSwapChain() && ensureShaders();
}

bool D3DVideoSurface::ensureSwapChain()
{
    if (width() <= 0 || height() <= 0) {
        return false;
    }
    if (m_swapChain && m_renderTargetView) {
        return true;
    }

    auto *device = D3D11VideoDevice::device();
    if (!device) {
        setError(QStringLiteral("D3D11 device is not available."));
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)
        || FAILED(dxgiDevice->GetAdapter(&adapter))
        || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        setError(QStringLiteral("Could not access DXGI factory: %1").arg(hresultText(hr)));
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = static_cast<UINT>(std::max(1, width()));
    desc.Height = static_cast<UINT>(std::max(1, height()));
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(device,
                                         reinterpret_cast<HWND>(winId()),
                                         &desc,
                                         nullptr,
                                         nullptr,
                                         &m_swapChain);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create video swap chain: %1").arg(hresultText(hr)));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not get swap chain back buffer: %1").arg(hresultText(hr)));
        return false;
    }

    hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create render target view: %1").arg(hresultText(hr)));
        return false;
    }

    return true;
}

bool D3DVideoSurface::ensureShaders()
{
    if (m_vertexShader
        && m_pixelShader
        && m_inputLayout
        && m_vertexBuffer
        && m_sampler
        && m_overlayVertexShader
        && m_overlayPixelShader
        && m_overlayInputLayout
        && m_overlayBlendState
        && m_overlayVertexBuffer) {
        return true;
    }

    auto *device = D3D11VideoDevice::device();
    if (!device) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(kVideoShader,
                            strlen(kVideoShader),
                            nullptr,
                            nullptr,
                            nullptr,
                            "vsMain",
                            "vs_4_0",
                            0,
                            0,
                            &vsBlob,
                            &errors);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not compile video vertex shader: %1").arg(hresultText(hr)));
        return false;
    }
    hr = D3DCompile(kVideoShader,
                    strlen(kVideoShader),
                    nullptr,
                    nullptr,
                    nullptr,
                    "psMain",
                    "ps_4_0",
                    0,
                    0,
                    &psBlob,
                    &errors);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not compile video pixel shader: %1").arg(hresultText(hr)));
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create video vertex shader: %1").arg(hresultText(hr)));
        return false;
    }
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create video pixel shader: %1").arg(hresultText(hr)));
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device->CreateInputLayout(layout,
                                   ARRAYSIZE(layout),
                                   vsBlob->GetBufferPointer(),
                                   vsBlob->GetBufferSize(),
                                   &m_inputLayout);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create video input layout: %1").arg(hresultText(hr)));
        return false;
    }

    D3D11_BUFFER_DESC vertexDesc = {};
    vertexDesc.ByteWidth = sizeof(Vertex) * 4;
    vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
    vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&vertexDesc, nullptr, &m_vertexBuffer);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create video vertex buffer: %1").arg(hresultText(hr)));
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&samplerDesc, &m_sampler);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create video sampler: %1").arg(hresultText(hr)));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> overlayVsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> overlayPsBlob;
    hr = D3DCompile(kOverlayShader,
                    strlen(kOverlayShader),
                    nullptr,
                    nullptr,
                    nullptr,
                    "vsMain",
                    "vs_4_0",
                    0,
                    0,
                    &overlayVsBlob,
                    &errors);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not compile hand overlay vertex shader: %1").arg(hresultText(hr)));
        return false;
    }
    hr = D3DCompile(kOverlayShader,
                    strlen(kOverlayShader),
                    nullptr,
                    nullptr,
                    nullptr,
                    "psMain",
                    "ps_4_0",
                    0,
                    0,
                    &overlayPsBlob,
                    &errors);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not compile hand overlay pixel shader: %1").arg(hresultText(hr)));
        return false;
    }

    hr = device->CreateVertexShader(overlayVsBlob->GetBufferPointer(),
                                    overlayVsBlob->GetBufferSize(),
                                    nullptr,
                                    &m_overlayVertexShader);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create hand overlay vertex shader: %1").arg(hresultText(hr)));
        return false;
    }
    hr = device->CreatePixelShader(overlayPsBlob->GetBufferPointer(),
                                   overlayPsBlob->GetBufferSize(),
                                   nullptr,
                                   &m_overlayPixelShader);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create hand overlay pixel shader: %1").arg(hresultText(hr)));
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC overlayLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device->CreateInputLayout(overlayLayout,
                                   ARRAYSIZE(overlayLayout),
                                   overlayVsBlob->GetBufferPointer(),
                                   overlayVsBlob->GetBufferSize(),
                                   &m_overlayInputLayout);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create hand overlay input layout: %1").arg(hresultText(hr)));
        return false;
    }

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blendDesc, &m_overlayBlendState);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create hand overlay blend state: %1").arg(hresultText(hr)));
        return false;
    }

    return ensureOverlayBuffer(1024);
}

bool D3DVideoSurface::ensureOverlayBuffer(int vertexCount)
{
    if (vertexCount <= m_overlayVertexCapacity && m_overlayVertexBuffer) {
        return true;
    }

    auto *device = D3D11VideoDevice::device();
    if (!device) {
        return false;
    }

    m_overlayVertexBuffer.Reset();
    m_overlayVertexCapacity = std::max(1024, vertexCount);

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(sizeof(OverlayVertex) * m_overlayVertexCapacity);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    const HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_overlayVertexBuffer);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create hand overlay vertex buffer: %1").arg(hresultText(hr)));
        return false;
    }
    return true;
}

bool D3DVideoSurface::ensureCopyTexture(const D3DFrame &frame)
{
    if (frame.format != DXGI_FORMAT_NV12 && frame.format != DXGI_FORMAT_P010) {
        setError(QStringLiteral("Unsupported D3D11 video texture format: %1").arg(static_cast<int>(frame.format)));
        return false;
    }

    if (m_copyTexture && m_copyWidth == frame.textureWidth && m_copyHeight == frame.textureHeight && m_copyFormat == frame.format) {
        return true;
    }

    m_lumaView.Reset();
    m_chromaView.Reset();
    m_copyTexture.Reset();
    m_copyWidth = frame.textureWidth;
    m_copyHeight = frame.textureHeight;
    m_copyFormat = frame.format;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(frame.textureWidth);
    desc.Height = static_cast<UINT>(frame.textureHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = frame.format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = D3D11VideoDevice::device()->CreateTexture2D(&desc, nullptr, &m_copyTexture);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create shader-readable video texture: %1").arg(hresultText(hr)));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC yDesc = {};
    yDesc.Format = frame.format == DXGI_FORMAT_P010 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    yDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    yDesc.Texture2D.MipLevels = 1;
    hr = D3D11VideoDevice::device()->CreateShaderResourceView(m_copyTexture.Get(), &yDesc, &m_lumaView);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create luma shader view: %1").arg(hresultText(hr)));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC uvDesc = {};
    uvDesc.Format = frame.format == DXGI_FORMAT_P010 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    uvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uvDesc.Texture2D.MipLevels = 1;
    hr = D3D11VideoDevice::device()->CreateShaderResourceView(m_copyTexture.Get(), &uvDesc, &m_chromaView);
    if (FAILED(hr)) {
        setError(QStringLiteral("Could not create chroma shader view: %1").arg(hresultText(hr)));
        return false;
    }

    return true;
}

bool D3DVideoSurface::uploadFrame(const D3DFrame &frame)
{
    D3D11VideoDevice::context()->CopySubresourceRegion(m_copyTexture.Get(),
                                                       0,
                                                       0,
                                                       0,
                                                       0,
                                                       frame.texture.Get(),
                                                       frame.arraySlice,
                                                       nullptr);
    return true;
}

void D3DVideoSurface::render(const D3DFrame &frame)
{
    auto *context = D3D11VideoDevice::context();
    constexpr float clearColor[] = {0.078f, 0.09f, 0.114f, 1.0f};
    context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(std::max(1, width()));
    viewport.Height = static_cast<float>(std::max(1, height()));
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    const float widgetAspect = viewport.Width / viewport.Height;
    const float frameAspect = frame.width > 0 && frame.height > 0
                                  ? static_cast<float>(frame.width) / static_cast<float>(frame.height)
                                  : widgetAspect;
    float u0 = 0.0f;
    float u1 = 1.0f;
    float v0 = 0.0f;
    float v1 = 1.0f;
    if (frameAspect > widgetAspect) {
        const float visibleWidth = widgetAspect / frameAspect;
        u0 = (1.0f - visibleWidth) * 0.5f;
        u1 = 1.0f - u0;
    } else if (frameAspect < widgetAspect) {
        const float visibleHeight = frameAspect / widgetAspect;
        v0 = (1.0f - visibleHeight) * 0.5f;
        v1 = 1.0f - v0;
    }

    const Vertex vertices[] = {
        {-1.0f, -1.0f, u0, v1},
        {-1.0f,  1.0f, u0, v0},
        { 1.0f, -1.0f, u1, v1},
        { 1.0f,  1.0f, u1, v0},
    };

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    memcpy(mapped.pData, vertices, sizeof(vertices));
    context->Unmap(m_vertexBuffer.Get(), 0);

    ID3D11RenderTargetView *rtv = m_renderTargetView.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer *vertexBuffer = m_vertexBuffer.Get();
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView *views[] = {m_lumaView.Get(), m_chromaView.Get()};
    context->PSSetShaderResources(0, 2, views);
    ID3D11SamplerState *samplers[] = {m_sampler.Get()};
    context->PSSetSamplers(0, 1, samplers);
    context->Draw(4, 0);

    ID3D11ShaderResourceView *nullViews[] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, nullViews);
    renderAthleteOverlay(frame, u0, u1, v0, v1, viewport);
    m_swapChain->Present(0, 0);
}

void D3DVideoSurface::renderAthleteOverlay(const D3DFrame &frame,
                                           float u0,
                                           float u1,
                                           float v0,
                                           float v1,
                                           const D3D11_VIEWPORT &viewport)
{
    if (m_athleteFrame.instances.isEmpty() || !m_overlayVertexBuffer) {
        return;
    }

    std::vector<OverlayVertex> vertices;
    vertices.reserve(512);
    for (const AthleteInstance &instance : m_athleteFrame.instances) {
        QSizeF sourceSize = m_athleteFrame.frameSize;
        if (sourceSize.width() <= 0.0 || sourceSize.height() <= 0.0) {
            sourceSize = QSizeF(frame.width, frame.height);
        }

        const OverlayColor color = colorForInstance(instance);
        if (instance.box.isValid()) {
            const QPointF corners[] = {
                instance.box.topLeft(),
                instance.box.topRight(),
                instance.box.bottomRight(),
                instance.box.bottomLeft(),
            };
            std::array<std::pair<float, float>, 4> mappedBox = {};
            std::array<bool, 4> boxVisible = {};
            for (int i = 0; i < 4; ++i) {
                float x = 0.0f;
                float y = 0.0f;
                boxVisible[static_cast<size_t>(i)] = mapLandmarkToNdc(corners[i],
                                                                      sourceSize,
                                                                      u0,
                                                                      u1,
                                                                      v0,
                                                                      v1,
                                                                      &x,
                                                                      &y);
                mappedBox[static_cast<size_t>(i)] = {x, y};
            }
            if (std::all_of(boxVisible.begin(), boxVisible.end(), [](bool visible) { return visible; })) {
                appendBox(&vertices, mappedBox, color);
            }
        }
    }

    if (vertices.empty()) {
        return;
    }
    if (!ensureOverlayBuffer(static_cast<int>(vertices.size()))) {
        return;
    }

    auto *context = D3D11VideoDevice::context();
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_overlayVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(OverlayVertex));
    context->Unmap(m_overlayVertexBuffer.Get(), 0);

    UINT stride = sizeof(OverlayVertex);
    UINT offset = 0;
    ID3D11Buffer *vertexBuffer = m_overlayVertexBuffer.Get();
    context->IASetInputLayout(m_overlayInputLayout.Get());
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->VSSetShader(m_overlayVertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_overlayPixelShader.Get(), nullptr, 0);

    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_overlayBlendState.Get(), blendFactor, 0xffffffff);
    context->Draw(static_cast<UINT>(vertices.size()), 0);
    context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
}

void D3DVideoSurface::releaseSizeDependentResources()
{
    m_renderTargetView.Reset();
    if (m_swapChain) {
        m_swapChain->ResizeBuffers(0,
                                   static_cast<UINT>(std::max(1, width())),
                                   static_cast<UINT>(std::max(1, height())),
                                   DXGI_FORMAT_UNKNOWN,
                                   0);
        m_swapChain.Reset();
    }
}

void D3DVideoSurface::setError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    qWarning() << "[D3DVideoSurface]" << error;
}
