#include "d3d11videodevice.h"

#include <QDebug>
#include <QMutex>

#include <d3d11.h>
#include <d3d11_4.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace {

QMutex g_deviceMutex;
Microsoft::WRL::ComPtr<ID3D11Device> g_device;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_context;
QString g_lastError;

QString hresultText(HRESULT hr)
{
    return QStringLiteral("HRESULT 0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)),
                                              8,
                                              16,
                                              QLatin1Char('0'));
}

} // namespace

bool D3D11VideoDevice::ensureInitialized(QString *error)
{
    QMutexLocker locker(&g_deviceMutex);
    if (g_device && g_context) {
        return true;
    }

    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL createdLevel = D3D_FEATURE_LEVEL_11_0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    const HRESULT hr = D3D11CreateDevice(nullptr,
                                         D3D_DRIVER_TYPE_HARDWARE,
                                         nullptr,
                                         flags,
                                         requestedLevels,
                                         ARRAYSIZE(requestedLevels),
                                         D3D11_SDK_VERSION,
                                         &device,
                                         &createdLevel,
                                         &context);
    if (FAILED(hr)) {
        g_lastError = QStringLiteral("Failed to create D3D11 video device: %1").arg(hresultText(hr));
        if (error) {
            *error = g_lastError;
        }
        qWarning() << "[D3D11VideoDevice]" << g_lastError;
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    g_device = device;
    g_context = context;
    g_lastError.clear();
    qDebug() << "[D3D11VideoDevice] initialized feature level"
             << QStringLiteral("0x%1").arg(static_cast<unsigned int>(createdLevel), 0, 16);
    return true;
}

ID3D11Device *D3D11VideoDevice::device()
{
    return ensureInitialized() ? g_device.Get() : nullptr;
}

ID3D11DeviceContext *D3D11VideoDevice::context()
{
    return ensureInitialized() ? g_context.Get() : nullptr;
}

AVBufferRef *D3D11VideoDevice::createFfmpegHwDevice(QString *error)
{
    if (!ensureInitialized(error)) {
        return nullptr;
    }

    AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!ref) {
        if (error) {
            *error = QStringLiteral("FFmpeg could not allocate a D3D11VA hardware device context.");
        }
        return nullptr;
    }

    auto *ctx = reinterpret_cast<AVHWDeviceContext *>(ref->data);
    auto *d3d = reinterpret_cast<AVD3D11VADeviceContext *>(ctx->hwctx);
    d3d->device = g_device.Get();
    d3d->device_context = g_context.Get();
    d3d->device->AddRef();
    d3d->device_context->AddRef();

    const int rc = av_hwdevice_ctx_init(ref);
    if (rc < 0) {
        av_buffer_unref(&ref);
        if (error) {
            *error = QStringLiteral("FFmpeg could not initialize the shared D3D11VA device context (%1).").arg(rc);
        }
        return nullptr;
    }

    return ref;
}
