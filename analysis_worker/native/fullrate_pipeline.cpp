#include <gst/gst.h>
#include <gstnvdsinfer.h>
#include <gstnvdsmeta.h>
#include <nvdsinfer.h>
#include <nvdsmeta.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kChunkDurationMs = 10000;
constexpr float kReidThreshold = 0.60F;
constexpr float kAmbiguousMargin = 0.05F;

struct Source
{
    std::string id;
    int cameraId = 0;
    fs::path path;
    std::int64_t sourceOffsetMs = 0;
    std::int64_t manualCorrectionMs = 0;
    std::int64_t lastCommittedFrame = -1;
    std::int64_t decodedFrames = 0;
};

struct GalleryEntry
{
    std::string athleteId;
    std::string label;
    std::vector<float> embedding;
};

struct Identity
{
    std::string athleteId;
    std::string label;
    std::string status = "unknown";
    float confidence = 0.0F;
};

struct Options
{
    fs::path outputRoot;
    fs::path galleryPath;
    std::string modelVersion;
    std::string preprocessingVersion;
    std::string pgieConfig = "/opt/iskating/config/pgie_yolo26x.txt";
    std::string sgieConfig = "/opt/iskating/config/sgie_personvit.txt";
    std::string trackerConfig = "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml";
    std::vector<Source> sources;
};

std::vector<std::string> split(const std::string &value, char delimiter)
{
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        parts.push_back(part);
    }
    return parts;
}

std::string jsonString(const std::string &value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
            } else {
                out << ch;
            }
        }
    }
    out << '"';
    return out.str();
}

Options parseOptions(int argc, char **argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for " + key);
        }
        const std::string value = argv[++index];
        if (key == "--output-root") options.outputRoot = value;
        else if (key == "--gallery") options.galleryPath = value;
        else if (key == "--model-version") options.modelVersion = value;
        else if (key == "--preprocessing-version") options.preprocessingVersion = value;
        else if (key == "--pgie-config") options.pgieConfig = value;
        else if (key == "--sgie-config") options.sgieConfig = value;
        else if (key == "--tracker-config") options.trackerConfig = value;
        else if (key == "--source") {
            const auto parts = split(value, ',');
            if (parts.size() != 6) throw std::runtime_error("--source requires six comma-separated fields");
            Source source;
            source.id = parts[0];
            source.cameraId = std::stoi(parts[1]);
            source.path = parts[2];
            source.sourceOffsetMs = std::stoll(parts[3]);
            source.manualCorrectionMs = std::stoll(parts[4]);
            source.lastCommittedFrame = std::stoll(parts[5]);
            options.sources.push_back(std::move(source));
        } else {
            throw std::runtime_error("unknown option: " + key);
        }
    }
    if (options.outputRoot.empty() || options.modelVersion.empty() || options.preprocessingVersion.empty()) {
        throw std::runtime_error("output root and model versions are required");
    }
    if (options.sources.size() != 12) {
        throw std::runtime_error("exactly 12 sources are required");
    }
    return options;
}

std::vector<GalleryEntry> loadGallery(const fs::path &path)
{
    std::vector<GalleryEntry> entries;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        const auto fields = split(line, '\t');
        if (fields.size() != 3) continue;
        GalleryEntry entry;
        entry.athleteId = fields[0];
        entry.label = fields[1];
        for (const std::string &value : split(fields[2], ',')) entry.embedding.push_back(std::stof(value));
        if (entry.embedding.size() == 768) entries.push_back(std::move(entry));
    }
    return entries;
}

Identity matchEmbedding(const std::vector<float> &embedding, const std::vector<GalleryEntry> &gallery)
{
    Identity identity;
    float norm = 0.0F;
    for (float value : embedding) norm += value * value;
    norm = std::sqrt(norm);
    float best = -1.0F;
    float second = -1.0F;
    const GalleryEntry *bestEntry = nullptr;
    for (const GalleryEntry &entry : gallery) {
        float dot = 0.0F;
        for (std::size_t index = 0; index < embedding.size(); ++index) dot += embedding[index] * entry.embedding[index];
        const float score = norm > 0.0F ? dot / norm : dot;
        if (score > best) {
            second = best;
            best = score;
            bestEntry = &entry;
        } else if (score > second) {
            second = score;
        }
    }
    identity.confidence = std::max(0.0F, best);
    if (!bestEntry || best < kReidThreshold) return identity;
    if (best - second < kAmbiguousMargin) {
        identity.status = "ambiguous";
        return identity;
    }
    identity.athleteId = bestEntry->athleteId;
    identity.label = bestEntry->label;
    identity.status = "identified";
    return identity;
}

std::vector<float> embeddingFromObject(NvDsObjectMeta *object)
{
    for (NvDsMetaList *item = object->obj_user_meta_list; item; item = item->next) {
        auto *userMeta = static_cast<NvDsUserMeta *>(item->data);
        if (!userMeta || userMeta->base_meta.meta_type != NVDSINFER_TENSOR_OUTPUT_META) continue;
        auto *tensor = static_cast<NvDsInferTensorMeta *>(userMeta->user_meta_data);
        if (!tensor) continue;
        for (unsigned int layerIndex = 0; layerIndex < tensor->num_output_layers; ++layerIndex) {
            const NvDsInferLayerInfo &layer = tensor->output_layers_info[layerIndex];
            std::size_t values = 1;
            for (unsigned int dim = 0; dim < layer.inferDims.numDims; ++dim) values *= layer.inferDims.d[dim];
            if (values != 768 || !tensor->out_buf_ptrs_host[layerIndex]) continue;
            const auto *buffer = static_cast<const float *>(tensor->out_buf_ptrs_host[layerIndex]);
            return {buffer, buffer + values};
        }
    }
    return {};
}

class ChunkWriter
{
public:
    ChunkWriter(const fs::path &root, Source *source, std::string modelVersion, std::string preprocessingVersion)
        : m_root(root), m_source(source), m_modelVersion(std::move(modelVersion)),
          m_preprocessingVersion(std::move(preprocessingVersion))
    {
    }

    void append(std::int64_t frameIndex, std::int64_t ptsMs, int objectCount, std::string line)
    {
        ++m_source->decodedFrames;
        if (frameIndex <= m_source->lastCommittedFrame) return;
        if (m_lines.empty()) {
            if (frameIndex != m_source->lastCommittedFrame + 1) {
                throw std::runtime_error("first uncommitted frame does not follow checkpoint for camera "
                                         + std::to_string(m_source->cameraId));
            }
            m_startFrame = frameIndex;
            m_startPts = ptsMs;
        } else if (frameIndex != m_endFrame + 1) {
            throw std::runtime_error("decoded frame sequence is not contiguous for camera " + std::to_string(m_source->cameraId));
        }
        m_endFrame = frameIndex;
        m_endPts = ptsMs;
        m_objectCount += objectCount;
        m_lines.push_back(std::move(line));
        if (m_endPts - m_startPts >= kChunkDurationMs) flush();
    }

    void flush()
    {
        if (m_lines.empty()) return;
        const std::int64_t hour = std::max<std::int64_t>(0, m_startPts / 3600000);
        const fs::path directory = m_root / ("camera" + cameraText()) / hourText(hour);
        fs::create_directories(directory);
        const std::string fileName = "chunk_" + std::to_string(m_startFrame) + "_" + std::to_string(m_endFrame) + ".jsonl.gz";
        const fs::path finalPath = directory / fileName;
        const fs::path temporaryPath = finalPath.string() + ".tmp";
        gzFile output = gzopen(temporaryPath.string().c_str(), "wb9");
        if (!output) throw std::runtime_error("cannot create analysis chunk " + temporaryPath.string());
        const std::string header = "{\"type\":\"header\",\"schemaVersion\":1,\"modelVersion\":"
            + jsonString(m_modelVersion) + ",\"preprocessingVersion\":" + jsonString(m_preprocessingVersion)
            + ",\"cameraId\":" + std::to_string(m_source->cameraId) + "}\n";
        gzwrite(output, header.data(), static_cast<unsigned int>(header.size()));
        for (const std::string &line : m_lines) {
            gzwrite(output, line.data(), static_cast<unsigned int>(line.size()));
            gzwrite(output, "\n", 1);
        }
        if (gzclose(output) != Z_OK) throw std::runtime_error("cannot finalize analysis chunk");
        fs::rename(temporaryPath, finalPath);
        const std::string relative = fs::relative(finalPath, m_root).generic_string();
        std::cout << "CHUNK\t" << m_source->id << '\t' << relative << '\t' << m_startFrame << '\t' << m_endFrame
                  << '\t' << m_startPts << '\t' << m_endPts << '\t' << m_lines.size() << '\t' << m_objectCount
                  << std::endl;
        m_lines.clear();
        m_objectCount = 0;
    }

private:
    std::string cameraText() const
    {
        std::ostringstream out;
        out << std::setw(2) << std::setfill('0') << m_source->cameraId;
        return out.str();
    }

    static std::string hourText(std::int64_t hour)
    {
        std::ostringstream out;
        out << std::setw(6) << std::setfill('0') << hour;
        return out.str();
    }

    fs::path m_root;
    Source *m_source;
    std::string m_modelVersion;
    std::string m_preprocessingVersion;
    std::vector<std::string> m_lines;
    std::int64_t m_startFrame = -1;
    std::int64_t m_endFrame = -1;
    std::int64_t m_startPts = -1;
    std::int64_t m_endPts = -1;
    int m_objectCount = 0;
};

struct Context
{
    Options options;
    std::vector<GalleryEntry> gallery;
    std::vector<ChunkWriter> writers;
    std::unordered_map<std::uint64_t, Identity> identities;
    std::string failure;
    std::mutex mutex;
};

std::uint64_t trackKey(int cameraId, std::uint64_t trackId)
{
    return (static_cast<std::uint64_t>(cameraId) << 56U) ^ trackId;
}

GstPadProbeReturn collectResults(GstPad *, GstPadProbeInfo *info, gpointer userData)
{
    auto *context = static_cast<Context *>(userData);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta *batch = gst_buffer_get_nvds_batch_meta(buffer);
    if (!batch) return GST_PAD_PROBE_OK;
    std::lock_guard<std::mutex> lock(context->mutex);
    for (NvDsMetaList *frameItem = batch->frame_meta_list; frameItem; frameItem = frameItem->next) {
        auto *frame = static_cast<NvDsFrameMeta *>(frameItem->data);
        if (!frame || frame->pad_index >= context->options.sources.size()) continue;
        Source &source = context->options.sources[frame->pad_index];
        const std::int64_t frameIndex = static_cast<std::int64_t>(frame->frame_num);
        const std::int64_t sourcePtsMs = static_cast<std::int64_t>(frame->buf_pts / GST_MSECOND);
        const std::int64_t batchTimeMs = std::max<std::int64_t>(0, sourcePtsMs + source.sourceOffsetMs + source.manualCorrectionMs);
        std::ostringstream line;
        line << "{\"frameIndex\":" << frameIndex << ",\"sourcePtsMs\":" << sourcePtsMs
             << ",\"batchTimeMs\":" << batchTimeMs << ",\"cameraId\":" << source.cameraId
             << ",\"width\":" << frame->source_frame_width << ",\"height\":" << frame->source_frame_height
             << ",\"objects\":[";
        int objectCount = 0;
        for (NvDsMetaList *objectItem = frame->obj_meta_list; objectItem; objectItem = objectItem->next) {
            auto *object = static_cast<NvDsObjectMeta *>(objectItem->data);
            if (!object || object->class_id != 0) continue;
            const std::vector<float> embedding = embeddingFromObject(object);
            const std::uint64_t key = trackKey(source.cameraId, object->object_id);
            bool reidExecuted = !embedding.empty();
            if (reidExecuted) context->identities[key] = matchEmbedding(embedding, context->gallery);
            const Identity identity = context->identities.count(key) ? context->identities[key] : Identity();
            if (objectCount++) line << ',';
            line << "{\"classId\":0,\"trackId\":" << object->object_id
                 << ",\"detectionConfidence\":" << object->confidence
                 << ",\"bboxX\":" << object->rect_params.left << ",\"bboxY\":" << object->rect_params.top
                 << ",\"bboxWidth\":" << object->rect_params.width << ",\"bboxHeight\":" << object->rect_params.height
                 << ",\"athleteId\":" << jsonString(identity.athleteId)
                 << ",\"label\":" << jsonString(identity.label)
                 << ",\"identityStatus\":" << jsonString(identity.status)
                 << ",\"identityConfidence\":" << identity.confidence
                 << ",\"identitySource\":\"personvit\",\"reidExecuted\":" << (reidExecuted ? "true" : "false") << '}';
        }
        line << "]}";
        try {
            context->writers[frame->pad_index].append(frameIndex, batchTimeMs, objectCount, line.str());
        } catch (const std::exception &error) {
            std::cerr << "writer error: " << error.what() << std::endl;
            context->failure = error.what();
            return GST_PAD_PROBE_DROP;
        }
    }
    return GST_PAD_PROBE_OK;
}

void decodePadAdded(GstElement *, GstPad *pad, gpointer userData)
{
    GstElement *bin = GST_ELEMENT(userData);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    GstCapsFeatures *features = caps && gst_caps_get_size(caps) > 0 ? gst_caps_get_features(caps, 0) : nullptr;
    if (features && gst_caps_features_contains(features, "memory:NVMM")) {
        GstPad *ghost = gst_element_get_static_pad(bin, "src");
        gst_ghost_pad_set_target(GST_GHOST_PAD(ghost), pad);
        gst_object_unref(ghost);
    }
    if (caps) gst_caps_unref(caps);
}

GstElement *createSourceBin(unsigned int index, const fs::path &path)
{
    const std::string name = "source-bin-" + std::to_string(index);
    GstElement *bin = gst_bin_new(name.c_str());
    GstElement *decoder = gst_element_factory_make("uridecodebin", (name + "-decode").c_str());
    if (!bin || !decoder) throw std::runtime_error("cannot create source decoder");
    GError *uriError = nullptr;
    gchar *uri = gst_filename_to_uri(fs::absolute(path).string().c_str(), &uriError);
    if (!uri) {
        const std::string message = uriError ? uriError->message : "unknown URI error";
        if (uriError) g_error_free(uriError);
        throw std::runtime_error("cannot create source URI: " + message);
    }
    g_object_set(decoder, "uri", uri, nullptr);
    g_free(uri);
    g_signal_connect(decoder, "pad-added", G_CALLBACK(decodePadAdded), bin);
    gst_bin_add(GST_BIN(bin), decoder);
    GstPad *ghost = gst_ghost_pad_new_no_target("src", GST_PAD_SRC);
    gst_element_add_pad(bin, ghost);
    return bin;
}

void setTrackerProperties(GstElement *tracker, const Options &options)
{
    g_object_set(tracker,
                 "ll-lib-file", "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so",
                 "ll-config-file", options.trackerConfig.c_str(),
                 "tracker-width", 960,
                 "tracker-height", 544,
                 "display-tracking-id", TRUE,
                 nullptr);
}

int run(Context *context)
{
    gst_init(nullptr, nullptr);
    GstElement *pipeline = gst_pipeline_new("iskating-fullrate");
    GstElement *mux = gst_element_factory_make("nvstreammux", "mux");
    GstElement *queue = gst_element_factory_make("queue", "inference-queue");
    GstElement *pgie = gst_element_factory_make("nvinfer", "yolo26x");
    GstElement *tracker = gst_element_factory_make("nvtracker", "tracker");
    GstElement *sgie = gst_element_factory_make("nvinfer", "personvit");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");
    if (!pipeline || !mux || !queue || !pgie || !tracker || !sgie || !sink) {
        throw std::runtime_error("required DeepStream plugins are unavailable");
    }
    g_object_set(mux, "batch-size", 12U, "live-source", FALSE, "batched-push-timeout", 40000, nullptr);
    g_object_set(queue, "leaky", 0, "max-size-buffers", 0U, "max-size-bytes", 0U, "max-size-time", 0U, nullptr);
    g_object_set(pgie, "config-file-path", context->options.pgieConfig.c_str(), "batch-size", 12U, "interval", 0U, nullptr);
    g_object_set(sgie, "config-file-path", context->options.sgieConfig.c_str(), "batch-size", 32U, nullptr);
    setTrackerProperties(tracker, context->options);
    g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);
    gst_bin_add_many(GST_BIN(pipeline), mux, queue, pgie, tracker, sgie, sink, nullptr);
    if (!gst_element_link_many(mux, queue, pgie, tracker, sgie, sink, nullptr)) {
        throw std::runtime_error("cannot link DeepStream inference chain");
    }
    for (unsigned int index = 0; index < context->options.sources.size(); ++index) {
        GstElement *sourceBin = createSourceBin(index, context->options.sources[index].path);
        gst_bin_add(GST_BIN(pipeline), sourceBin);
        const std::string sinkName = "sink_" + std::to_string(index);
        GstPad *sinkPad = gst_element_request_pad_simple(mux, sinkName.c_str());
        GstPad *sourcePad = gst_element_get_static_pad(sourceBin, "src");
        if (!sinkPad || !sourcePad || gst_pad_link(sourcePad, sinkPad) != GST_PAD_LINK_OK) {
            throw std::runtime_error("cannot link camera " + std::to_string(index + 1) + " to stream mux");
        }
        gst_object_unref(sourcePad);
        gst_object_unref(sinkPad);
    }
    GstPad *probePad = gst_element_get_static_pad(sgie, "src");
    gst_pad_add_probe(probePad, GST_PAD_PROBE_TYPE_BUFFER, collectResults, context, nullptr);
    gst_object_unref(probePad);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        throw std::runtime_error("cannot start DeepStream pipeline");
    }
    int result = 0;
    GstBus *bus = gst_element_get_bus(pipeline);
    bool running = true;
    while (running) {
        GstMessage *message = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (!message) continue;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            std::cerr << "DeepStream error: " << (error ? error->message : "unknown") << std::endl;
            if (debug) std::cerr << debug << std::endl;
            if (error) g_error_free(error);
            g_free(debug);
            result = 1;
        }
        running = false;
        gst_message_unref(message);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    if (!context->failure.empty()) return 1;
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        Context context;
        context.options = parseOptions(argc, argv);
        context.gallery = loadGallery(context.options.galleryPath);
        for (Source &source : context.options.sources) {
            context.writers.emplace_back(context.options.outputRoot, &source,
                                         context.options.modelVersion, context.options.preprocessingVersion);
        }
        const int result = run(&context);
        if (result != 0) return result;
        for (ChunkWriter &writer : context.writers) writer.flush();
        for (const Source &source : context.options.sources) {
            std::cout << "FINISH\t" << source.id << '\t' << source.decodedFrames << std::endl;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "fatal: " << error.what() << std::endl;
        return 1;
    }
}
