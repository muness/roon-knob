#include "kizz_detector_aot.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>

#include "esp_log.h"
#include "esp_nn.h"
#include "esp_nn_ansi_headers.h"
#include "tensorflow/lite/kernels/internal/quantization_util.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr char TAG[] = "kizz_detector_aot";
constexpr size_t kAlignment = 16;

class ArenaCursor final {
 public:
    ArenaCursor(uint8_t *data, size_t bytes) : data_(data), bytes_(bytes) {}

    template <typename T>
    T *allocate(size_t count = 1) {
        const size_t aligned = (offset_ + kAlignment - 1) & ~(kAlignment - 1);
        const size_t requested = sizeof(T) * count;
        if (!data_ || aligned > bytes_ || requested > bytes_ - aligned)
            return nullptr;
        T *result = reinterpret_cast<T *>(data_ + aligned);
        offset_ = aligned + requested;
        return result;
    }

    size_t used() const { return offset_; }

 private:
    uint8_t *data_;
    size_t bytes_;
    size_t offset_{0};
};

bool shape_is(const tflite::Tensor *tensor,
              std::initializer_list<int32_t> expected) {
    if (!tensor || !tensor->shape() ||
        tensor->shape()->size() != expected.size())
        return false;
    size_t index = 0;
    for (const int32_t value : expected) {
        if (tensor->shape()->Get(index++) != value) return false;
    }
    return true;
}

float tensor_scale(const tflite::Tensor *tensor) {
    const auto *quant = tensor ? tensor->quantization() : nullptr;
    return quant && quant->scale() && quant->scale()->size() > 0
        ? quant->scale()->Get(0)
        : 0.0f;
}

int32_t tensor_zero_point(const tflite::Tensor *tensor) {
    const auto *quant = tensor ? tensor->quantization() : nullptr;
    return quant && quant->zero_point() && quant->zero_point()->size() > 0
        ? static_cast<int32_t>(quant->zero_point()->Get(0))
        : 0;
}

template <typename T>
const T *constant_data(const tflite::Model *model,
                       const tflite::Tensor *tensor, size_t count) {
    if (!model || !model->buffers() || !tensor) return nullptr;
    const auto *buffer = model->buffers()->Get(tensor->buffer());
    const auto *data = buffer ? buffer->data() : nullptr;
    if (!data || data->size() < count * sizeof(T)) return nullptr;
    return reinterpret_cast<const T *>(data->Data());
}

struct MirroredHistory {
    int8_t *data{nullptr};
    size_t frames{0};
    size_t channels{0};
    size_t next{0};

    bool initialize(ArenaCursor *arena, size_t frame_count,
                    size_t channel_count) {
        frames = frame_count;
        channels = channel_count;
        data = arena->allocate<int8_t>(2 * frames * channels);
        return data != nullptr;
    }

    void reset(int8_t zero_point) {
        std::memset(data, zero_point, 2 * frames * channels);
        next = 0;
    }

    void push(const int8_t *values, size_t count = 1) {
        for (size_t frame = 0; frame < count; ++frame) {
            const int8_t *source = values + frame * channels;
            std::memcpy(data + next * channels, source, channels);
            std::memcpy(data + (next + frames) * channels, source, channels);
            next = (next + 1) % frames;
        }
    }

    const int8_t *window() const { return data + next * channels; }
};

}  // namespace

struct KizzDetectorAot::Impl {
    struct Conv {
        const int8_t *weights{nullptr};
        const int32_t *bias{nullptr};
        data_dims_t input_dims{};
        data_dims_t filter_dims{};
        data_dims_t output_dims{};
        conv_params_t params{};
        quant_data_t quant{};
    };

    struct Depthwise {
        const int8_t *weights{nullptr};
        const int32_t *bias{nullptr};
        data_dims_t input_dims{};
        data_dims_t filter_dims{};
        data_dims_t output_dims{};
        dw_conv_params_t params{};
        quant_data_t quant{};
    };

    std::array<Conv, 6> convs{};
    std::array<Depthwise, 4> depthwise{};
    std::array<MirroredHistory, 5> histories{};
    int8_t *depth_output{nullptr};
    int8_t *point_output{nullptr};
    int8_t *logits{nullptr};
};

namespace {

bool prepare_quantization(const tflite::Tensor *input,
                          const tflite::Tensor *weights,
                          const tflite::Tensor *output,
                          int32_t *multipliers, int32_t *shifts,
                          size_t channels) {
    const auto *quant = weights ? weights->quantization() : nullptr;
    if (!input || !weights || !output || !multipliers || !shifts ||
        !quant || !quant->scale() || quant->scale()->size() != channels)
        return false;
    const double input_scale = tensor_scale(input);
    const double output_scale = tensor_scale(output);
    if (!(input_scale > 0.0) || !(output_scale > 0.0)) return false;
    for (size_t channel = 0; channel < channels; ++channel) {
        const double multiplier = input_scale * quant->scale()->Get(channel) /
                                  output_scale;
        int shift = 0;
        tflite::QuantizeMultiplier(multiplier, &multipliers[channel], &shift);
        shifts[channel] = shift;
    }
    return true;
}

bool prepare_conv(KizzDetectorAot::Impl::Conv *op, ArenaCursor *arena,
                  const tflite::Model *model,
                  const tflite::SubGraph *graph,
                  int input_index, int weights_index, int bias_index,
                  int output_index, int input_height, int input_channels,
                  int filter_height, int output_channels,
                  int stride_height, bool relu) {
    const auto *input = graph->tensors()->Get(input_index);
    const auto *weights = graph->tensors()->Get(weights_index);
    const auto *bias = graph->tensors()->Get(bias_index);
    const auto *output = graph->tensors()->Get(output_index);
    if (!shape_is(input, {1, input_height, 1, input_channels}) ||
        !shape_is(weights,
                  {output_channels, filter_height, 1, input_channels}) ||
        !shape_is(bias, {output_channels}) ||
        !shape_is(output, {1, 1, 1, output_channels}))
        return false;
    op->weights = constant_data<int8_t>(
        model, weights, output_channels * filter_height * input_channels);
    op->bias = constant_data<int32_t>(model, bias, output_channels);
    int32_t *multipliers = arena->allocate<int32_t>(output_channels);
    int32_t *shifts = arena->allocate<int32_t>(output_channels);
    if (!op->weights || !op->bias || !multipliers || !shifts ||
        !prepare_quantization(input, weights, output, multipliers, shifts,
                              output_channels))
        return false;
    op->input_dims = {1, input_height, input_channels, 1};
    op->filter_dims = {1, filter_height, input_channels, 0};
    op->output_dims = {1, 1, output_channels, 1};
    op->params = {
        -tensor_zero_point(input), tensor_zero_point(output),
        {1, stride_height}, {0, 0}, {0, 0},
        {relu ? tensor_zero_point(output) : -128, 127}};
    op->quant = {shifts, multipliers};
    return true;
}

bool prepare_depthwise(KizzDetectorAot::Impl::Depthwise *op,
                       ArenaCursor *arena, const tflite::Model *model,
                       const tflite::SubGraph *graph,
                       int input_index, int weights_index, int bias_index,
                       int output_index, int height, int channels) {
    const auto *input = graph->tensors()->Get(input_index);
    const auto *weights = graph->tensors()->Get(weights_index);
    const auto *bias = graph->tensors()->Get(bias_index);
    const auto *output = graph->tensors()->Get(output_index);
    if (!shape_is(input, {1, height, 1, channels}) ||
        !shape_is(weights, {1, height, 1, channels}) ||
        !shape_is(bias, {channels}) ||
        !shape_is(output, {1, 1, 1, channels}))
        return false;
    op->weights = constant_data<int8_t>(model, weights, height * channels);
    op->bias = constant_data<int32_t>(model, bias, channels);
    int32_t *multipliers = arena->allocate<int32_t>(channels);
    int32_t *shifts = arena->allocate<int32_t>(channels);
    if (!op->weights || !op->bias || !multipliers || !shifts ||
        !prepare_quantization(input, weights, output, multipliers, shifts,
                              channels))
        return false;
    op->input_dims = {1, height, channels, 1};
    op->filter_dims = {1, height, channels, 0};
    op->output_dims = {1, 1, channels, 1};
    op->params = {-tensor_zero_point(input), tensor_zero_point(output), 1,
                  {1, 1}, {0, 0}, {0, 0}, {-128, 127}};
    op->quant = {shifts, multipliers};
    return true;
}

void run_conv(const KizzDetectorAot::Impl::Conv &op, const int8_t *input,
              int8_t *output) {
    esp_nn_conv_s8_ansi(&op.input_dims, input, &op.filter_dims, op.weights,
                        op.bias, &op.output_dims, output, &op.params,
                        &op.quant);
}

void run_depthwise(const KizzDetectorAot::Impl::Depthwise &op,
                   const int8_t *input, int8_t *output) {
    esp_nn_depthwise_conv_s8_ansi(
        &op.input_dims, input, &op.filter_dims, op.weights, op.bias,
        &op.output_dims, output, &op.params, &op.quant);
}

}  // namespace

bool KizzDetectorAot::initialize(const tflite::Model *model, uint8_t *arena,
                                 size_t arena_bytes) {
    impl_ = nullptr;
    arena_used_bytes_ = 0;
    scratch_bytes_ = 0;
    if (!model || model->version() != TFLITE_SCHEMA_VERSION || !arena ||
        arena_bytes == 0 || !model->subgraphs() ||
        model->subgraphs()->size() != 2)
        return false;
    const auto *graph = model->subgraphs()->Get(0);
    if (!graph || !graph->tensors() || graph->tensors()->size() != 64 ||
        !graph->operators() || graph->operators()->size() != 39)
        return false;

    ArenaCursor cursor(arena, arena_bytes);
    Impl *impl = cursor.allocate<Impl>();
    if (!impl) return false;
    new (impl) Impl();

    const bool graph_ok =
        prepare_conv(&impl->convs[0], &cursor, model, graph,
                     38, 30, 29, 40, 5, 40, 5, 48, 3, true) &&
        prepare_depthwise(&impl->depthwise[0], &cursor, model, graph,
                          42, 28, 27, 43, 3, 48) &&
        prepare_conv(&impl->convs[1], &cursor, model, graph,
                     43, 26, 25, 44, 1, 48, 1, 96, 1, true) &&
        prepare_depthwise(&impl->depthwise[1], &cursor, model, graph,
                          47, 24, 23, 48, 5, 96) &&
        prepare_conv(&impl->convs[2], &cursor, model, graph,
                     48, 22, 21, 49, 1, 96, 1, 96, 1, true) &&
        prepare_depthwise(&impl->depthwise[2], &cursor, model, graph,
                          52, 20, 19, 53, 7, 96) &&
        prepare_conv(&impl->convs[3], &cursor, model, graph,
                     53, 18, 17, 54, 1, 96, 1, 96, 1, true) &&
        prepare_depthwise(&impl->depthwise[3], &cursor, model, graph,
                          57, 16, 15, 58, 9, 96) &&
        prepare_conv(&impl->convs[4], &cursor, model, graph,
                     58, 14, 13, 59, 1, 96, 1, 96, 1, true) &&
        prepare_conv(&impl->convs[5], &cursor, model, graph,
                     59, 12, 11, 60, 1, 96, 1, 12, 1, false);
    if (!graph_ok) return false;

    const std::array<size_t, 5> frames{5, 3, 5, 7, 9};
    const std::array<size_t, 5> channels{40, 48, 96, 96, 96};
    for (size_t index = 0; index < impl->histories.size(); ++index) {
        if (!impl->histories[index].initialize(
                &cursor, frames[index], channels[index]))
            return false;
    }
    impl->depth_output = cursor.allocate<int8_t>(96);
    impl->point_output = cursor.allocate<int8_t>(96);
    impl->logits = cursor.allocate<int8_t>(kOutputValues);
    if (!impl->depth_output || !impl->point_output || !impl->logits)
        return false;

    const auto *input = graph->tensors()->Get(0);
    const auto *output = graph->tensors()->Get(63);
    if (!shape_is(input, {1, 3, 40}) ||
        input->type() != tflite::TensorType_INT8 ||
        !shape_is(output, {1, 1, 12}) ||
        output->type() != tflite::TensorType_UINT8)
        return false;

    impl_ = impl;
    arena_used_bytes_ = cursor.used();
    scratch_bytes_ = 0;
    input_scale_ = tensor_scale(input);
    input_zero_point_ = static_cast<int8_t>(tensor_zero_point(input));
    output_scale_ = tensor_scale(output);
    output_zero_point_ = tensor_zero_point(output);
    reset();
    ESP_LOGI(TAG,
             "qualified v5c fixed graph ready: arena_used=%u scratch=%u",
             static_cast<unsigned>(arena_used_bytes_),
             static_cast<unsigned>(scratch_bytes_));
    return true;
}

void KizzDetectorAot::reset() {
    if (!impl_) return;
    for (auto &history : impl_->histories) history.reset(-128);
    std::memset(impl_->depth_output, 0, 96);
    std::memset(impl_->point_output, 0, 96);
    std::memset(impl_->logits, 0, kOutputValues);
}

bool KizzDetectorAot::invoke(const int8_t input[kInputValues],
                             uint8_t output[kOutputValues]) {
    if (!impl_ || !input || !output) return false;
    impl_->histories[0].push(input, kInputFrames);
    run_conv(impl_->convs[0], impl_->histories[0].window(),
             impl_->point_output);

    for (size_t block = 0; block < impl_->depthwise.size(); ++block) {
        impl_->histories[block + 1].push(impl_->point_output);
        run_depthwise(impl_->depthwise[block],
                      impl_->histories[block + 1].window(),
                      impl_->depth_output);
        run_conv(impl_->convs[block + 1], impl_->depth_output,
                 impl_->point_output);
    }

    run_conv(impl_->convs[5], impl_->point_output, impl_->logits);
    for (size_t index = 0; index < kOutputValues; ++index) {
        output[index] = static_cast<uint8_t>(
            static_cast<int32_t>(impl_->logits[index]) + 128);
    }
    return true;
}
