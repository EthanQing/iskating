#include "tensorrtrunner.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSaveFile>
#include <QStringList>

#include <NvInferPlugin.h>
#include <NvOnnxParser.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

void configureDllSearchPaths()
{
#ifdef Q_OS_WIN
    static bool configured = false;
    if (configured) {
        return;
    }
    configured = true;

    const QStringList paths = {
        QCoreApplication::applicationDirPath(),
        QStringLiteral("C:/Program Files/TensorRT-10.1.0.27/lib"),
        QStringLiteral("C:/Program Files/TensorRT-10.1.0.27/bin"),
        QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/bin"),
    };
    for (const QString &path : paths) {
        AddDllDirectory(reinterpret_cast<PCWSTR>(path.utf16()));
    }
#endif
}

class TrtLogger final : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char *msg) noexcept override
    {
        if (severity <= Severity::kWARNING) {
            qWarning() << "[TensorRT]" << msg;
        }
    }
};

template <typename T>
struct TrtDestroy
{
    void operator()(T *ptr) const
    {
        delete ptr;
    }
};

template <typename T>
using TrtPtr = std::unique_ptr<T, TrtDestroy<T>>;

QString cudaErrorText(cudaError_t rc)
{
    return QString::fromLatin1(cudaGetErrorString(rc));
}

size_t dimsElementCount(const nvinfer1::Dims &dims)
{
    size_t count = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) {
            return 0;
        }
        count *= static_cast<size_t>(dims.d[i]);
    }
    return count;
}

size_t dataTypeSize(nvinfer1::DataType type)
{
    switch (type) {
    case nvinfer1::DataType::kFLOAT:
        return 4;
    case nvinfer1::DataType::kHALF:
        return 2;
    case nvinfer1::DataType::kINT8:
        return 1;
    case nvinfer1::DataType::kINT32:
        return 4;
    case nvinfer1::DataType::kBOOL:
        return 1;
    case nvinfer1::DataType::kUINT8:
        return 1;
    default:
        return 4;
    }
}

QString dimsText(const nvinfer1::Dims &dims)
{
    QStringList parts;
    for (int i = 0; i < dims.nbDims; ++i) {
        parts.append(QString::number(dims.d[i]));
    }
    return QStringLiteral("[%1]").arg(parts.join(QLatin1Char('x')));
}

QString enginePathFor(const QString &onnxPath)
{
    QFileInfo info(onnxPath);
    return info.dir().absoluteFilePath(info.completeBaseName() + QStringLiteral(".fp16.engine"));
}

bool writeEngineFile(const QString &path, const void *data, qsizetype size, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法写入 TensorRT engine：%1").arg(path);
        }
        return false;
    }
    file.write(static_cast<const char *>(data), size);
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("保存 TensorRT engine 失败：%1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace

struct TensorRtRunner::Impl
{
    ~Impl()
    {
        if (stream) {
            cudaStreamDestroy(stream);
        }
        for (void *buffer : deviceBuffers) {
            cudaFree(buffer);
        }
    }

    bool buildEngine(const QString &onnxPath, const QString &enginePath, QByteArray *engineData, QString *error)
    {
        TrtPtr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger));
        if (!builder) {
            if (error) {
                *error = QStringLiteral("创建 TensorRT builder 失败");
            }
            return false;
        }

        const auto flags = 0U;
        TrtPtr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(flags));
        if (!network) {
            if (error) {
                *error = QStringLiteral("创建 TensorRT network 失败");
            }
            return false;
        }

        TrtPtr<nvonnxparser::IParser> parser(nvonnxparser::createParser(*network, logger));
        if (!parser || !parser->parseFromFile(onnxPath.toLocal8Bit().constData(), static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
            QStringList parseErrors;
            if (parser) {
                for (int i = 0; i < parser->getNbErrors(); ++i) {
                    parseErrors.append(QString::fromLatin1(parser->getError(i)->desc()));
                }
            }
            if (error) {
                *error = QStringLiteral("解析 ONNX 失败：%1 %2").arg(onnxPath, parseErrors.join(QStringLiteral("; ")));
            }
            return false;
        }

        TrtPtr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());
        if (!config) {
            if (error) {
                *error = QStringLiteral("创建 TensorRT builder config 失败");
            }
            return false;
        }
        config->setFlag(nvinfer1::BuilderFlag::kFP16);

        TrtPtr<nvinfer1::IHostMemory> serialized(builder->buildSerializedNetwork(*network, *config));
        if (!serialized) {
            if (error) {
                *error = QStringLiteral("构建 TensorRT engine 失败：%1").arg(onnxPath);
            }
            return false;
        }

        if (!writeEngineFile(enginePath, serialized->data(), static_cast<qsizetype>(serialized->size()), error)) {
            return false;
        }
        *engineData = QByteArray(static_cast<const char *>(serialized->data()),
                                 static_cast<qsizetype>(serialized->size()));
        qDebug() << "[TensorRT] built engine" << enginePath;
        return true;
    }

    bool allocateIo(QString *error)
    {
        const int tensorCount = engine->getNbIOTensors();
        deviceBuffers.assign(static_cast<size_t>(tensorCount), nullptr);
        hostBufferSizes.assign(static_cast<size_t>(tensorCount), 0);
        tensorNames.assign(static_cast<size_t>(tensorCount), {});
        tensorDims.assign(static_cast<size_t>(tensorCount), {});
        for (int i = 0; i < tensorCount; ++i) {
            const char *name = engine->getIOTensorName(i);
            const auto dims = engine->getTensorShape(name);
            const auto type = engine->getTensorDataType(name);
            const size_t bytes = dimsElementCount(dims) * dataTypeSize(type);
            if (bytes == 0) {
                if (error) {
                    *error = QStringLiteral("TensorRT IO 维度无效：%1 %2").arg(QString::fromLatin1(name), dimsText(dims));
                }
                return false;
            }
            void *buffer = nullptr;
            const cudaError_t rc = cudaMalloc(&buffer, bytes);
            if (rc != cudaSuccess) {
                if (error) {
                    *error = QStringLiteral("分配 TensorRT IO 缓冲失败：%1").arg(cudaErrorText(rc));
                }
                return false;
            }
            deviceBuffers[static_cast<size_t>(i)] = buffer;
            hostBufferSizes[static_cast<size_t>(i)] = bytes;
            tensorNames[static_cast<size_t>(i)] = QString::fromLatin1(name);
            tensorDims[static_cast<size_t>(i)] = dims;
            if (!context->setTensorAddress(name, buffer)) {
                if (error) {
                    *error = QStringLiteral("设置 TensorRT tensor address 失败：%1").arg(QString::fromLatin1(name));
                }
                return false;
            }

            if (engine->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT) {
                inputIndex = i;
                inputName = QString::fromLatin1(name);
                inputDims = dims;
            } else {
                outputIndices.push_back(i);
            }
        }
        qDebug() << "[TensorRT] loaded" << inputName << ioSummary();
        return true;
    }

    QString ioSummary() const
    {
        QStringList parts;
        for (int i = 0; i < engine->getNbIOTensors(); ++i) {
            const char *name = engine->getIOTensorName(i);
            const auto mode = engine->getTensorIOMode(name);
            parts.append(QStringLiteral("%1 %2 %3")
                             .arg(mode == nvinfer1::TensorIOMode::kINPUT ? QStringLiteral("IN") : QStringLiteral("OUT"),
                                  QString::fromLatin1(name),
                                  dimsText(engine->getTensorShape(name))));
        }
        return parts.join(QStringLiteral("; "));
    }

    TrtLogger logger;
    TrtPtr<nvinfer1::IRuntime> runtime;
    TrtPtr<nvinfer1::ICudaEngine> engine;
    TrtPtr<nvinfer1::IExecutionContext> context;
    cudaStream_t stream = nullptr;
    std::vector<void *> deviceBuffers;
    std::vector<size_t> hostBufferSizes;
    std::vector<QString> tensorNames;
    std::vector<nvinfer1::Dims> tensorDims;
    std::vector<int> outputIndices;
    int inputIndex = -1;
    QString inputName;
    nvinfer1::Dims inputDims{};
};

TensorRtRunner::TensorRtRunner()
    : m_impl(std::make_unique<Impl>())
{
}

TensorRtRunner::~TensorRtRunner() = default;

bool TensorRtRunner::initialize(const QString &onnxPath, QString *error)
{
    configureDllSearchPaths();
    initLibNvInferPlugins(&m_impl->logger, "");

    if (!QFileInfo::exists(onnxPath)) {
        if (error) {
            *error = QStringLiteral("模型文件不存在：%1").arg(onnxPath);
        }
        return false;
    }

    const QString enginePath = enginePathFor(onnxPath);
    QByteArray engineData;
    if (QFileInfo::exists(enginePath)) {
        qDebug() << "[TensorRT] loading cached engine" << enginePath;
        QFile file(enginePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral("无法读取 TensorRT engine：%1").arg(enginePath);
            }
            return false;
        }
        engineData = file.readAll();
    } else {
        qWarning() << "[TensorRT] cached engine not found; building may take several minutes" << enginePath;
        if (!m_impl->buildEngine(onnxPath, enginePath, &engineData, error)) {
            return false;
        }
    }

    m_impl->runtime.reset(nvinfer1::createInferRuntime(m_impl->logger));
    if (!m_impl->runtime) {
        if (error) {
            *error = QStringLiteral("创建 TensorRT runtime 失败");
        }
        return false;
    }
    m_impl->engine.reset(m_impl->runtime->deserializeCudaEngine(engineData.constData(), static_cast<size_t>(engineData.size())));
    if (!m_impl->engine) {
        if (error) {
            *error = QStringLiteral("反序列化 TensorRT engine 失败：%1").arg(enginePath);
        }
        return false;
    }
    m_impl->context.reset(m_impl->engine->createExecutionContext());
    if (!m_impl->context) {
        if (error) {
            *error = QStringLiteral("创建 TensorRT execution context 失败");
        }
        return false;
    }
    const cudaError_t streamRc = cudaStreamCreate(&m_impl->stream);
    if (streamRc != cudaSuccess) {
        if (error) {
            *error = QStringLiteral("创建 CUDA stream 失败：%1").arg(cudaErrorText(streamRc));
        }
        return false;
    }

    return m_impl->allocateIo(error);
}

bool TensorRtRunner::infer(const std::vector<float> &input, std::vector<TensorRtOutput> *outputs, QString *error)
{
    if (!m_impl->engine || !m_impl->context || m_impl->inputIndex < 0) {
        if (error) {
            *error = QStringLiteral("TensorRT runner 未初始化");
        }
        return false;
    }
    if (input.size() != m_impl->hostBufferSizes[static_cast<size_t>(m_impl->inputIndex)] / sizeof(float)) {
        if (error) {
            *error = QStringLiteral("TensorRT 输入尺寸不匹配");
        }
        return false;
    }

    cudaError_t rc = cudaMemcpyAsync(m_impl->deviceBuffers[static_cast<size_t>(m_impl->inputIndex)],
                                     input.data(),
                                     input.size() * sizeof(float),
                                     cudaMemcpyHostToDevice,
                                     m_impl->stream);
    if (rc != cudaSuccess) {
        if (error) {
            *error = QStringLiteral("复制 TensorRT 输入失败：%1").arg(cudaErrorText(rc));
        }
        return false;
    }

    if (!m_impl->context->enqueueV3(m_impl->stream)) {
        if (error) {
            *error = QStringLiteral("TensorRT 推理执行失败");
        }
        return false;
    }

    outputs->clear();
    for (int outputIndex : m_impl->outputIndices) {
        const size_t bytes = m_impl->hostBufferSizes[static_cast<size_t>(outputIndex)];
        std::vector<float> host(bytes / sizeof(float), 0.0f);
        const QString outputName = m_impl->tensorNames[static_cast<size_t>(outputIndex)];
        rc = cudaMemcpyAsync(host.data(),
                             m_impl->deviceBuffers[static_cast<size_t>(outputIndex)],
                             bytes,
                             cudaMemcpyDeviceToHost,
                             m_impl->stream);
        if (rc != cudaSuccess) {
            if (error) {
                *error = QStringLiteral("复制 TensorRT 输出失败：%1").arg(cudaErrorText(rc));
            }
            return false;
        }
        TensorRtOutput output;
        output.name = outputName;
        output.dims = m_impl->tensorDims[static_cast<size_t>(outputIndex)];
        output.values = std::move(host);
        outputs->push_back(std::move(output));
    }

    rc = cudaStreamSynchronize(m_impl->stream);
    if (rc != cudaSuccess) {
        if (error) {
            *error = QStringLiteral("等待 TensorRT 推理完成失败：%1").arg(cudaErrorText(rc));
        }
        return false;
    }
    return true;
}

QSize TensorRtRunner::inputImageSize() const
{
    if (m_impl->inputDims.nbDims == 4) {
        const int c1 = static_cast<int>(m_impl->inputDims.d[1]);
        const int h = static_cast<int>(m_impl->inputDims.d[2]);
        const int w = static_cast<int>(m_impl->inputDims.d[3]);
        if (c1 == 3) {
            return QSize(w, h);
        }
        return QSize(static_cast<int>(m_impl->inputDims.d[2]), static_cast<int>(m_impl->inputDims.d[1]));
    }
    return QSize(0, 0);
}

QString TensorRtRunner::ioSummary() const
{
    return m_impl->engine ? m_impl->ioSummary() : QString();
}

std::vector<float> imageToNhwcFloat(const QImage &image, const QSize &targetSize)
{
    const QImage scaled = image.convertToFormat(QImage::Format_RGB888)
                              .scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> result(static_cast<size_t>(targetSize.width() * targetSize.height() * 3), 0.0f);
    size_t index = 0;
    for (int y = 0; y < scaled.height(); ++y) {
        const uchar *line = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            result[index++] = line[x * 3 + 0] / 255.0f;
            result[index++] = line[x * 3 + 1] / 255.0f;
            result[index++] = line[x * 3 + 2] / 255.0f;
        }
    }
    return result;
}

std::vector<float> imageToNchwFloat(const QImage &image, const QSize &targetSize)
{
    const QImage scaled = image.convertToFormat(QImage::Format_RGB888)
                              .scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int planeSize = targetSize.width() * targetSize.height();
    std::vector<float> result(static_cast<size_t>(planeSize * 3), 0.0f);
    for (int y = 0; y < scaled.height(); ++y) {
        const uchar *line = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            const int pixelIndex = y * scaled.width() + x;
            result[static_cast<size_t>(pixelIndex)] = line[x * 3 + 0] / 255.0f;
            result[static_cast<size_t>(planeSize + pixelIndex)] = line[x * 3 + 1] / 255.0f;
            result[static_cast<size_t>(planeSize * 2 + pixelIndex)] = line[x * 3 + 2] / 255.0f;
        }
    }
    return result;
}
