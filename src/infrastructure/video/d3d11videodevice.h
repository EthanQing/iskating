#ifndef D3D11VIDEODEVICE_H
#define D3D11VIDEODEVICE_H

#include <QString>

#include <d3d11.h>

extern "C" {
#include <libavutil/buffer.h>
}

class D3D11VideoDevice
{
public:
    static bool ensureInitialized(QString *error = nullptr);
    static ID3D11Device *device();
    static ID3D11DeviceContext *context();
    static AVBufferRef *createFfmpegHwDevice(QString *error = nullptr);
};

#endif // D3D11VIDEODEVICE_H
