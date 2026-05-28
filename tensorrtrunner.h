#ifndef TENSORRTRUNNER_H
#define TENSORRTRUNNER_H

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>
#include <vector>

struct TensorRtOutput
{
    QString name;
    nvinfer1::Dims dims{};
    std::vector<float> values;
};

class TensorRtRunner
{
public:
    TensorRtRunner();
    ~TensorRtRunner();

    bool initialize(const QString &onnxPath, QString *error);
    bool infer(const std::vector<float> &input, std::vector<TensorRtOutput> *outputs, QString *error);
    QSize inputImageSize() const;
    QString ioSummary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

std::vector<float> imageToNhwcFloat(const QImage &image, const QSize &targetSize);

#endif // TENSORRTRUNNER_H
