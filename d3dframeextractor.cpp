#include "d3dframeextractor.h"

#include "d3d11videodevice.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

QString hresultText(HRESULT hr)
{
    return QStringLiteral("HRESULT 0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)),
                                              8,
                                              16,
                                              QLatin1Char('0'));
}

uchar clampByte(float value)
{
    return static_cast<uchar>(std::clamp(std::lround(value), 0L, 255L));
}

void convertNv12ToRgb(const uchar *base,
                      int rowPitch,
                      int width,
                      int height,
                      QImage *rgb)
{
    for (int y = 0; y < height; ++y) {
        const uchar *yLine = base + y * rowPitch;
        const uchar *uvLine = base + rowPitch * height + (y / 2) * rowPitch;
        uchar *out = rgb->scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int uvIndex = (x / 2) * 2;
            const float yy = 1.164383f * (static_cast<float>(yLine[x]) - 16.0f);
            const float uu = static_cast<float>(uvLine[uvIndex + 0]) - 128.0f;
            const float vv = static_cast<float>(uvLine[uvIndex + 1]) - 128.0f;
            out[x * 3 + 0] = clampByte(yy + 1.596027f * vv);
            out[x * 3 + 1] = clampByte(yy - 0.391762f * uu - 0.812968f * vv);
            out[x * 3 + 2] = clampByte(yy + 2.017232f * uu);
        }
    }
}

void convertP010ToRgb(const uchar *base,
                      int rowPitch,
                      int width,
                      int height,
                      QImage *rgb)
{
    for (int y = 0; y < height; ++y) {
        const auto *yLine = reinterpret_cast<const quint16 *>(base + y * rowPitch);
        const auto *uvLine = reinterpret_cast<const quint16 *>(base + rowPitch * height + (y / 2) * rowPitch);
        uchar *out = rgb->scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int uvIndex = (x / 2) * 2;
            const float y8 = static_cast<float>(yLine[x] >> 8);
            const float u8 = static_cast<float>(uvLine[uvIndex + 0] >> 8);
            const float v8 = static_cast<float>(uvLine[uvIndex + 1] >> 8);
            const float yy = 1.164383f * (y8 - 16.0f);
            const float uu = u8 - 128.0f;
            const float vv = v8 - 128.0f;
            out[x * 3 + 0] = clampByte(yy + 1.596027f * vv);
            out[x * 3 + 1] = clampByte(yy - 0.391762f * uu - 0.812968f * vv);
            out[x * 3 + 2] = clampByte(yy + 2.017232f * uu);
        }
    }
}

} // namespace

QImage D3DFrameExtractor::copyToRgb(const std::shared_ptr<D3DFrame> &frame, QString *error)
{
    if (!frame || !frame->texture) {
        if (error) {
            *error = QStringLiteral("没有可分析的视频帧");
        }
        return {};
    }
    if (!ensureStagingTexture(*frame, error)) {
        return {};
    }

    auto *context = D3D11VideoDevice::context();
    if (!context) {
        if (error) {
            *error = QStringLiteral("D3D11 context 不可用");
        }
        return {};
    }

    context->CopySubresourceRegion(m_stagingTexture.Get(),
                                   0,
                                   0,
                                   0,
                                   0,
                                   frame->texture.Get(),
                                   frame->arraySlice,
                                   nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        if (error) {
            *error = QStringLiteral("读取 D3D11 staging 纹理失败：%1").arg(hresultText(hr));
        }
        return {};
    }

    QImage rgb(frame->width, frame->height, QImage::Format_RGB888);
    if (frame->format == DXGI_FORMAT_NV12) {
        convertNv12ToRgb(static_cast<const uchar *>(mapped.pData),
                         static_cast<int>(mapped.RowPitch),
                         frame->width,
                         frame->height,
                         &rgb);
    } else if (frame->format == DXGI_FORMAT_P010) {
        convertP010ToRgb(static_cast<const uchar *>(mapped.pData),
                         static_cast<int>(mapped.RowPitch),
                         frame->width,
                         frame->height,
                         &rgb);
    } else {
        if (error) {
            *error = QStringLiteral("AI readback 暂不支持视频格式：%1").arg(static_cast<int>(frame->format));
        }
        rgb = {};
    }

    context->Unmap(m_stagingTexture.Get(), 0);
    return rgb;
}

bool D3DFrameExtractor::ensureStagingTexture(const D3DFrame &frame, QString *error)
{
    if (frame.format != DXGI_FORMAT_NV12 && frame.format != DXGI_FORMAT_P010) {
        if (error) {
            *error = QStringLiteral("AI readback 暂不支持视频格式：%1").arg(static_cast<int>(frame.format));
        }
        return false;
    }

    if (m_stagingTexture
        && m_width == frame.textureWidth
        && m_height == frame.textureHeight
        && m_format == frame.format) {
        return true;
    }

    auto *device = D3D11VideoDevice::device();
    if (!device) {
        if (error) {
            *error = QStringLiteral("D3D11 device 不可用");
        }
        return false;
    }

    m_stagingTexture.Reset();
    m_width = frame.textureWidth;
    m_height = frame.textureHeight;
    m_format = frame.format;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(frame.textureWidth);
    desc.Height = static_cast<UINT>(frame.textureHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = frame.format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    const HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_stagingTexture);
    if (FAILED(hr)) {
        if (error) {
            *error = QStringLiteral("创建 AI staging 纹理失败：%1").arg(hresultText(hr));
        }
        return false;
    }
    return true;
}
