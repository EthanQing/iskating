#ifndef D3DVIDEOSURFACE_H
#define D3DVIDEOSURFACE_H

#include "d3dframe.h"
#include "handposeresult.h"

#include <QWidget>
#include <QVector>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>

class D3DVideoSurface : public QWidget
{
public:
    explicit D3DVideoSurface(QWidget *parent = nullptr);
    ~D3DVideoSurface() override;

    void presentFrame(const std::shared_ptr<D3DFrame> &frame);
    void clearFrame();
    void setHandPoseResults(const QVector<HandPoseResult> &results);
    QString lastError() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    bool ensureRenderResources();
    bool ensureSwapChain();
    bool ensureShaders();
    bool ensureOverlayBuffer(int vertexCount);
    bool ensureCopyTexture(const D3DFrame &frame);
    bool uploadFrame(const D3DFrame &frame);
    void render(const D3DFrame &frame);
    void renderHandPoseOverlay(const D3DFrame &frame, float u0, float u1, float v0, float v1, const D3D11_VIEWPORT &viewport);
    void releaseSizeDependentResources();
    void setError(const QString &error);

    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_copyTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_lumaView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_chromaView;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_overlayVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_overlayPixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_overlayInputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_overlayVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_overlayBlendState;
    int m_overlayVertexCapacity = 0;
    DXGI_FORMAT m_copyFormat = DXGI_FORMAT_UNKNOWN;
    int m_copyWidth = 0;
    int m_copyHeight = 0;
    QString m_lastError;
    QVector<HandPoseResult> m_handPoseResults;
};

#endif // D3DVIDEOSURFACE_H
