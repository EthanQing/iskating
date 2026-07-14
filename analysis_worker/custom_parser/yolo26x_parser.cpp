#include "nvdsinfer_custom_impl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

extern "C" bool NvDsInferParseYolo26x(const std::vector<NvDsInferLayerInfo> &outputLayers,
                                       const NvDsInferNetworkInfo &networkInfo,
                                       const NvDsInferParseDetectionParams &detectionParams,
                                       std::vector<NvDsInferParseObjectInfo> &objects)
{
    const NvDsInferLayerInfo *output = nullptr;
    for (const NvDsInferLayerInfo &layer : outputLayers) {
        std::size_t values = 1;
        for (unsigned int index = 0; index < layer.inferDims.numDims; ++index) {
            values *= static_cast<std::size_t>(layer.inferDims.d[index]);
        }
        if (values == 300U * 6U) {
            output = &layer;
            break;
        }
    }
    if (!output || !output->buffer) {
        return false;
    }

    const float threshold = detectionParams.perClassPreclusterThreshold.empty()
                                ? 0.35F
                                : detectionParams.perClassPreclusterThreshold.front();
    const auto *values = static_cast<const float *>(output->buffer);
    objects.reserve(objects.size() + 32);
    for (int index = 0; index < 300; ++index) {
        const float *candidate = values + index * 6;
        const int classId = static_cast<int>(std::lround(candidate[5]));
        if (classId != 0 || candidate[4] < threshold) {
            continue;
        }
        const float left = std::clamp(candidate[0], 0.0F, static_cast<float>(networkInfo.width));
        const float top = std::clamp(candidate[1], 0.0F, static_cast<float>(networkInfo.height));
        const float right = std::clamp(candidate[2], 0.0F, static_cast<float>(networkInfo.width));
        const float bottom = std::clamp(candidate[3], 0.0F, static_cast<float>(networkInfo.height));
        if (right - left <= 1.0F || bottom - top <= 1.0F) {
            continue;
        }
        NvDsInferParseObjectInfo object = {};
        object.left = left;
        object.top = top;
        object.width = right - left;
        object.height = bottom - top;
        object.classId = 0;
        object.detectionConfidence = candidate[4];
        objects.push_back(object);
    }
    return true;
}

CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseYolo26x);

extern "C" bool NvDsInferParsePersonVit(const std::vector<NvDsInferLayerInfo> &,
                                         const NvDsInferNetworkInfo &,
                                         float,
                                         std::vector<NvDsInferAttribute> &,
                                         std::string &)
{
    return true;
}

CHECK_CUSTOM_CLASSIFIER_PARSE_FUNC_PROTOTYPE(NvDsInferParsePersonVit);
