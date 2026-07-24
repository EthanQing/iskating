#ifndef D3DFRAME_H
#define D3DFRAME_H

#include <QDateTime>

#include <d3d11.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

struct D3DFrame
{
    ~D3DFrame()
    {
        if (frame) {
            av_frame_free(&frame);
        }
    }

    D3DFrame(const D3DFrame &) = delete;
    D3DFrame &operator=(const D3DFrame &) = delete;

    static std::shared_ptr<D3DFrame> fromAvFrame(const AVFrame *source, qint64 mediaTimeMs = -1)
    {
        if (!source || source->format != AV_PIX_FMT_D3D11 || !source->data[0]) {
            return {};
        }

        auto result = std::shared_ptr<D3DFrame>(new D3DFrame);
        result->frame = av_frame_alloc();
        if (!result->frame || av_frame_ref(result->frame, source) < 0) {
            return {};
        }

        auto *texture = reinterpret_cast<ID3D11Texture2D *>(source->data[0]);
        result->texture = texture;
        result->arraySlice = static_cast<UINT>(reinterpret_cast<intptr_t>(source->data[1]));
        result->width = source->width;
        result->height = source->height;
        result->pts = source->pts;
        result->mediaTimeMs = mediaTimeMs;
        result->receivedMsec = QDateTime::currentMSecsSinceEpoch();

        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        result->format = desc.Format;
        result->textureWidth = static_cast<int>(desc.Width);
        result->textureHeight = static_cast<int>(desc.Height);
        return result;
    }

    AVFrame *frame = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    UINT arraySlice = 0;
    int width = 0;
    int height = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int64_t pts = AV_NOPTS_VALUE;
    qint64 mediaTimeMs = -1;
    qint64 receivedMsec = 0;

private:
    D3DFrame() = default;
};

#endif // D3DFRAME_H
