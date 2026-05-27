#ifndef D3DFRAMEEXTRACTOR_H
#define D3DFRAMEEXTRACTOR_H

#include "d3dframe.h"

#include <QImage>
#include <QString>

#include <d3d11.h>
#include <wrl/client.h>

#include <memory>

class D3DFrameExtractor
{
public:
    QImage copyToRgb(const std::shared_ptr<D3DFrame> &frame, QString *error = nullptr);

private:
    bool ensureStagingTexture(const D3DFrame &frame, QString *error);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTexture;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    int m_width = 0;
    int m_height = 0;
};

#endif // D3DFRAMEEXTRACTOR_H
