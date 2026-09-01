#include "kizz_verifier_aot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>

#include "esp_log.h"
#include "esp_nn.h"
#include "esp_nn_ansi_headers.h"
#include "tensorflow/lite/kernels/internal/common.h"
#include "tensorflow/lite/kernels/internal/quantization_util.h"
#include "tensorflow/lite/kernels/internal/reference/integer_ops/conv.h"
#include "tensorflow/lite/kernels/internal/reference/integer_ops/depthwise_conv.h"
#include "tensorflow/lite/kernels/internal/reference/integer_ops/fully_connected.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

extern "C" void esp_nn_conv_s8_1x1(
    const int8_t *input, uint16_t input_width, uint16_t input_height,
    uint16_t input_channels, int32_t input_offset,
    const int8_t *filter_data, const int32_t *bias, int8_t *output,
    uint16_t output_channels, int32_t output_offset,
    const int32_t *output_shift, const int32_t *output_multiplier,
    int32_t activation_min, int32_t activation_max, void *scratch);

namespace {

constexpr char TAG[] = "kizz_verifier_aot";
constexpr size_t kAlignment = 16;
constexpr size_t kLargeActivationBytes = 130 * 20 * 24;
constexpr size_t kSmallActivationBytes = 65 * 10 * 24;

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
    size_t capacity() const { return bytes_; }

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

}  // namespace

struct KizzVerifierAot::Impl {
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

    std::array<Conv, 5> convs{};
    std::array<Depthwise, 4> depthwise{};
    int8_t *large{nullptr};
    int8_t *small{nullptr};
    const int8_t *dense_weights{nullptr};
    const int32_t *dense_bias{nullptr};
    int32_t dense_multiplier{0};
    int dense_shift{0};
    int32_t dense_input_zero{0};
    int32_t dense_filter_zero{0};
    int32_t dense_output_zero{0};
    float dense_output_scale{0.0f};
    float tanh_output_scale{0.0f};
    int32_t tanh_output_zero{0};
    float multiply_constant{0.0f};
    float output_scale{0.0f};
    int32_t output_zero{0};
    size_t scratch_bytes{0};
    bool traced{false};
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

bool prepare_conv(KizzVerifierAot::Impl::Conv *op, ArenaCursor *arena,
                  const tflite::Model *model, const tflite::SubGraph *graph,
                  int input_index, int weights_index, int bias_index,
                  int output_index, int input_height, int input_width,
                  int input_channels, int filter_height, int filter_width,
                  int output_height, int output_width, int output_channels,
                  int stride_height, int stride_width, int pad_height,
                  int pad_width) {
    const auto *input = graph->tensors()->Get(input_index);
    const auto *weights = graph->tensors()->Get(weights_index);
    const auto *bias = graph->tensors()->Get(bias_index);
    const auto *output = graph->tensors()->Get(output_index);
    if (!shape_is(input, {1, input_height, input_width, input_channels}) ||
        !shape_is(weights, {output_channels, filter_height, filter_width,
                            input_channels}) ||
        !shape_is(bias, {output_channels}) ||
        !shape_is(output, {1, output_height, output_width, output_channels}))
        return false;
    op->weights = constant_data<int8_t>(
        model, weights, output_channels * filter_height * filter_width *
                            input_channels);
    op->bias = constant_data<int32_t>(model, bias, output_channels);
    int32_t *multipliers = arena->allocate<int32_t>(output_channels);
    int32_t *shifts = arena->allocate<int32_t>(output_channels);
    if (!op->weights || !op->bias || !multipliers || !shifts ||
        !prepare_quantization(input, weights, output, multipliers, shifts,
                              output_channels))
        return false;
    op->input_dims = {input_width, input_height, input_channels, 1};
    op->filter_dims = {filter_width, filter_height, input_channels, 0};
    op->output_dims = {output_width, output_height, output_channels, 1};
    op->params = {
        -tensor_zero_point(input), tensor_zero_point(output),
        {stride_width, stride_height}, {pad_width, pad_height}, {0, 0},
        {tensor_zero_point(output), 127}};
    op->quant = {shifts, multipliers};
    return true;
}

bool prepare_depthwise(KizzVerifierAot::Impl::Depthwise *op,
                       ArenaCursor *arena, const tflite::Model *model,
                       const tflite::SubGraph *graph, int input_index,
                       int weights_index, int bias_index, int output_index,
                       int input_height, int input_width, int channels,
                       int output_height, int output_width, int pad_height,
                       int pad_width) {
    const auto *input = graph->tensors()->Get(input_index);
    const auto *weights = graph->tensors()->Get(weights_index);
    const auto *bias = graph->tensors()->Get(bias_index);
    const auto *output = graph->tensors()->Get(output_index);
    if (!shape_is(input, {1, input_height, input_width, channels}) ||
        !shape_is(weights, {1, 3, 3, channels}) ||
        !shape_is(bias, {channels}) ||
        !shape_is(output, {1, output_height, output_width, channels}))
        return false;
    op->weights = constant_data<int8_t>(model, weights, 3 * 3 * channels);
    op->bias = constant_data<int32_t>(model, bias, channels);
    int32_t *multipliers = arena->allocate<int32_t>(channels);
    int32_t *shifts = arena->allocate<int32_t>(channels);
    if (!op->weights || !op->bias || !multipliers || !shifts ||
        !prepare_quantization(input, weights, output, multipliers, shifts,
                              channels))
        return false;
    op->input_dims = {input_width, input_height, channels, 1};
    op->filter_dims = {3, 3, channels, 0};
    op->output_dims = {output_width, output_height, channels, 1};
    op->params = {-tensor_zero_point(input), tensor_zero_point(output), 1,
                  {2, 2}, {pad_width, pad_height}, {0, 0},
                  {tensor_zero_point(output), 127}};
    op->quant = {shifts, multipliers};
    return true;
}

void run_conv(const KizzVerifierAot::Impl::Conv &op, const int8_t *input,
              int8_t *output, void *workspace, bool align_weights) {
    const int8_t *weights = op.weights;
    uint8_t *scratch = static_cast<uint8_t *>(workspace);
    if (align_weights) {
        const size_t weight_bytes = static_cast<size_t>(op.filter_dims.width) *
            op.filter_dims.height * op.filter_dims.channels *
            op.output_dims.channels;
        std::memcpy(scratch, op.weights, weight_bytes);
        weights = reinterpret_cast<const int8_t *>(scratch);
        scratch += (weight_bytes + kAlignment - 1) & ~(kAlignment - 1);
    }
    esp_nn_set_conv_scratch_buf(scratch);
    esp_nn_conv_s8(&op.input_dims, input, &op.filter_dims, weights,
                   op.bias, &op.output_dims, output, &op.params, &op.quant);
}

void run_depthwise(const KizzVerifierAot::Impl::Depthwise &op,
                   const int8_t *input, int8_t *output, void *scratch) {
    esp_nn_set_depthwise_conv_scratch_buf(scratch);
    esp_nn_depthwise_conv_s8(&op.input_dims, input, &op.filter_dims,
                             op.weights, op.bias, &op.output_dims, output,
                             &op.params, &op.quant);
}

void run_depthwise_ansi(const KizzVerifierAot::Impl::Depthwise &op,
                        const int8_t *input, int8_t *output) {
    esp_nn_depthwise_conv_s8_ansi(&op.input_dims, input, &op.filter_dims,
                                  op.weights, op.bias, &op.output_dims, output,
                                  &op.params, &op.quant);
}

void run_conv_ansi(const KizzVerifierAot::Impl::Conv &op,
                   const int8_t *input, int8_t *output) {
    esp_nn_conv_s8_ansi(&op.input_dims, input, &op.filter_dims, op.weights,
                        op.bias, &op.output_dims, output, &op.params,
                        &op.quant);
}

void run_conv_1x1(const KizzVerifierAot::Impl::Conv &op,
                  const int8_t *input, int8_t *output, void *scratch) {
    esp_nn_conv_s8_1x1(
        input, op.input_dims.width, op.input_dims.height,
        op.input_dims.channels, op.params.in_offset, op.weights, op.bias,
        output, op.output_dims.channels, op.params.out_offset,
        op.quant.shift, op.quant.mult, op.params.activation.min,
        op.params.activation.max, scratch);
    // The direct 1x1 routine leaves QACC holding its final output channel.
    // Subsequent ESP-NN kernels assume an empty accumulator bank.
    __asm__ volatile("ee.zero.qacc" ::: "memory");
}

tflite::RuntimeShape shape4(int32_t d0, int32_t d1, int32_t d2,
                            int32_t d3) {
    const int32_t dims[] = {d0, d1, d2, d3};
    return tflite::RuntimeShape(4, dims);
}

tflite::RuntimeShape shape2(int32_t d0, int32_t d1) {
    const int32_t dims[] = {d0, d1};
    return tflite::RuntimeShape(2, dims);
}

tflite::RuntimeShape shape1(int32_t d0) {
    return tflite::RuntimeShape(1, &d0);
}

void run_conv_reference(const KizzVerifierAot::Impl::Conv &op,
                        const int8_t *input, int8_t *output) {
    tflite::ConvParams params{};
    params.padding_type = tflite::PaddingType::kSame;
    params.padding_values.width = op.params.padding.width;
    params.padding_values.height = op.params.padding.height;
    params.stride_width = op.params.stride.width;
    params.stride_height = op.params.stride.height;
    params.dilation_width_factor = 1;
    params.dilation_height_factor = 1;
    params.input_offset = op.params.in_offset;
    params.output_offset = op.params.out_offset;
    params.quantized_activation_min = op.params.activation.min;
    params.quantized_activation_max = op.params.activation.max;
    const auto input_shape = shape4(1, op.input_dims.height,
                                    op.input_dims.width,
                                    op.input_dims.channels);
    const auto filter_shape = shape4(op.output_dims.channels,
                                     op.filter_dims.height,
                                     op.filter_dims.width,
                                     op.filter_dims.channels);
    const auto bias_shape = shape1(op.output_dims.channels);
    const auto output_shape = shape4(1, op.output_dims.height,
                                     op.output_dims.width,
                                     op.output_dims.channels);
    tflite::reference_integer_ops::ConvPerChannel(
        params, op.quant.mult, op.quant.shift, input_shape, input,
        filter_shape, op.weights, bias_shape, op.bias, output_shape, output);
}

void run_depthwise_reference(const KizzVerifierAot::Impl::Depthwise &op,
                             const int8_t *input, int8_t *output) {
    tflite::DepthwiseParams params{};
    params.padding_type = tflite::PaddingType::kSame;
    params.padding_values.width = op.params.padding.width;
    params.padding_values.height = op.params.padding.height;
    params.stride_width = op.params.stride.width;
    params.stride_height = op.params.stride.height;
    params.dilation_width_factor = 1;
    params.dilation_height_factor = 1;
    params.depth_multiplier = op.params.ch_mult;
    params.input_offset = op.params.in_offset;
    params.output_offset = op.params.out_offset;
    params.quantized_activation_min = op.params.activation.min;
    params.quantized_activation_max = op.params.activation.max;
    const auto input_shape = shape4(1, op.input_dims.height,
                                    op.input_dims.width,
                                    op.input_dims.channels);
    const auto filter_shape = shape4(1, op.filter_dims.height,
                                     op.filter_dims.width,
                                     op.output_dims.channels);
    const auto bias_shape = shape1(op.output_dims.channels);
    const auto output_shape = shape4(1, op.output_dims.height,
                                     op.output_dims.width,
                                     op.output_dims.channels);
    tflite::reference_integer_ops::DepthwiseConvPerChannel(
        params, op.quant.mult, op.quant.shift, input_shape, input,
        filter_shape, op.weights, bias_shape, op.bias, output_shape, output);
}

void run_fully_connected_reference(
        const int8_t *input, const int8_t *weights, const int32_t *bias,
        int32_t input_offset, int32_t weights_offset, int32_t output_offset,
        int32_t output_multiplier, int output_shift, int8_t *output) {
    tflite::FullyConnectedParams params{};
    params.input_offset = input_offset;
    params.weights_offset = weights_offset;
    params.output_offset = output_offset;
    params.output_multiplier = output_multiplier;
    params.output_shift = output_shift;
    params.quantized_activation_min = -128;
    params.quantized_activation_max = 127;
    const auto input_shape = shape2(1, 1728);
    const auto filter_shape = shape2(1, 1728);
    const auto bias_shape = shape1(1);
    const auto output_shape = shape2(1, 1);
    tflite::reference_integer_ops::FullyConnected<
        int8_t, int8_t, int8_t, int32_t>(
            params, input_shape, input, filter_shape, weights, bias_shape,
            bias, output_shape, output);
}

int8_t quantize_int8(float value, float scale, int32_t zero_point) {
    const long quantized = std::lround(value / scale) + zero_point;
    return static_cast<int8_t>(std::clamp<long>(quantized, -128, 127));
}

void trace_tensor(const char *name, const int8_t *data, size_t count) {
    uint32_t hash = 2166136261u;
    int32_t minimum = 127;
    int32_t maximum = -128;
    int64_t sum = 0;
    for (size_t index = 0; index < count; ++index) {
        const int32_t value = data[index];
        hash = (hash ^ static_cast<uint8_t>(value)) * 16777619u;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        sum += value;
    }
    ESP_LOGI(TAG,
             "AOT_TRACE tensor=%s count=%u fnv=%08lx min=%ld max=%ld sum=%lld",
             name, static_cast<unsigned>(count),
             static_cast<unsigned long>(hash), static_cast<long>(minimum),
             static_cast<long>(maximum), static_cast<long long>(sum));
}

}  // namespace

bool KizzVerifierAot::initialize(const tflite::Model *model, uint8_t *arena,
                                 size_t arena_bytes) {
    impl_ = nullptr;
    arena_used_bytes_ = 0;
    if (!model || model->version() != TFLITE_SCHEMA_VERSION || !arena ||
        !model->subgraphs() || model->subgraphs()->size() != 1) {
        ESP_LOGE(TAG, "init failed: invalid model/schema/arena/subgraph");
        return false;
    }
    const auto *graph = model->subgraphs()->Get(0);
    if (!graph || !graph->tensors() || graph->tensors()->size() != 36 ||
        !graph->operators() || graph->operators()->size() != 13) {
        ESP_LOGE(TAG, "init failed: graph contract tensors=%u ops=%u",
                 graph && graph->tensors()
                     ? static_cast<unsigned>(graph->tensors()->size()) : 0,
                 graph && graph->operators()
                     ? static_cast<unsigned>(graph->operators()->size()) : 0);
        return false;
    }

    ArenaCursor cursor(arena, arena_bytes);
    Impl *impl = cursor.allocate<Impl>();
    if (!impl) {
        ESP_LOGE(TAG, "init failed: impl allocation arena=%u",
                 static_cast<unsigned>(arena_bytes));
        return false;
    }
    new (impl) Impl();

#define PREPARE_OR_FAIL(name, expression)                                  \
    do {                                                                    \
        if (!(expression)) {                                                \
            ESP_LOGE(TAG, "init failed: %s used=%u/%u", name,             \
                     static_cast<unsigned>(cursor.used()),                  \
                     static_cast<unsigned>(cursor.capacity()));            \
            return false;                                                   \
        }                                                                   \
    } while (false)

    PREPARE_OR_FAIL("stem",
        prepare_conv(&impl->convs[0], &cursor, model, graph,
                     0, 22, 21, 23, 260, 40, 1, 5, 5, 130, 20, 24,
                     2, 2, 1, 1));
    PREPARE_OR_FAIL("dw1",
        prepare_depthwise(&impl->depthwise[0], &cursor, model, graph,
                          23, 20, 19, 24, 130, 20, 24, 65, 10, 0, 0));
    PREPARE_OR_FAIL("pw1",
        prepare_conv(&impl->convs[1], &cursor, model, graph,
                     24, 18, 17, 25, 65, 10, 24, 1, 1, 65, 10, 32,
                     1, 1, 0, 0));
    PREPARE_OR_FAIL("dw2",
        prepare_depthwise(&impl->depthwise[1], &cursor, model, graph,
                          25, 16, 15, 26, 65, 10, 32, 33, 5, 1, 0));
    PREPARE_OR_FAIL("pw2",
        prepare_conv(&impl->convs[2], &cursor, model, graph,
                     26, 14, 13, 27, 33, 5, 32, 1, 1, 33, 5, 48,
                     1, 1, 0, 0));
    PREPARE_OR_FAIL("dw3",
        prepare_depthwise(&impl->depthwise[2], &cursor, model, graph,
                          27, 12, 11, 28, 33, 5, 48, 17, 3, 1, 1));
    PREPARE_OR_FAIL("pw3",
        prepare_conv(&impl->convs[3], &cursor, model, graph,
                     28, 10, 9, 29, 17, 3, 48, 1, 1, 17, 3, 64,
                     1, 1, 0, 0));
    PREPARE_OR_FAIL("dw4",
        prepare_depthwise(&impl->depthwise[3], &cursor, model, graph,
                          29, 8, 7, 30, 17, 3, 64, 9, 2, 1, 1));
    PREPARE_OR_FAIL("pw4",
        prepare_conv(&impl->convs[4], &cursor, model, graph,
                     30, 6, 5, 31, 9, 2, 64, 1, 1, 9, 2, 96,
                     1, 1, 0, 0));
#undef PREPARE_OR_FAIL

    impl->large = cursor.allocate<int8_t>(kLargeActivationBytes);
    impl->small = cursor.allocate<int8_t>(kSmallActivationBytes);
    const auto *dense_input = graph->tensors()->Get(32);
    const auto *dense_weights = graph->tensors()->Get(4);
    const auto *dense_bias = graph->tensors()->Get(3);
    const auto *dense_output = graph->tensors()->Get(33);
    const auto *tanh_output = graph->tensors()->Get(34);
    const auto *multiply_constant = graph->tensors()->Get(2);
    const auto *output = graph->tensors()->Get(35);
    const int8_t *constant = constant_data<int8_t>(
        model, multiply_constant, 1);
    impl->dense_weights = constant_data<int8_t>(model, dense_weights, 1728);
    impl->dense_bias = constant_data<int32_t>(model, dense_bias, 1);
    if (!impl->large || !impl->small || !impl->dense_weights ||
        !impl->dense_bias || !constant ||
        !shape_is(dense_input, {1, 1728}) ||
        !shape_is(dense_weights, {1, 1728}) ||
        !shape_is(dense_bias, {1}) ||
        !shape_is(dense_output, {1, 1}) ||
        !shape_is(tanh_output, {1, 1}) ||
        !shape_is(output, {1, 1})) {
        ESP_LOGE(TAG, "init failed: activations/dense used=%u/%u",
                 static_cast<unsigned>(cursor.used()),
                 static_cast<unsigned>(cursor.capacity()));
        return false;
    }
    const double dense_real_multiplier =
        tensor_scale(dense_input) * tensor_scale(dense_weights) /
        tensor_scale(dense_output);
    tflite::QuantizeMultiplier(dense_real_multiplier,
                               &impl->dense_multiplier,
                               &impl->dense_shift);
    impl->dense_input_zero = tensor_zero_point(dense_input);
    impl->dense_filter_zero = tensor_zero_point(dense_weights);
    impl->dense_output_zero = tensor_zero_point(dense_output);
    impl->dense_output_scale = tensor_scale(dense_output);
    impl->tanh_output_scale = tensor_scale(tanh_output);
    impl->tanh_output_zero = tensor_zero_point(tanh_output);
    impl->multiply_constant =
        (static_cast<int32_t>(*constant) -
         tensor_zero_point(multiply_constant)) *
        tensor_scale(multiply_constant);
    impl->output_scale = tensor_scale(output);
    impl->output_zero = tensor_zero_point(output);
    if (!(impl->dense_output_scale > 0.0f) ||
        !(impl->tanh_output_scale > 0.0f) ||
        !(impl->output_scale > 0.0f)) {
        ESP_LOGE(TAG, "init failed: invalid output scale");
        return false;
    }

    constexpr std::array<size_t, 5> kConvScratchCapacity = {
        kSmallActivationBytes - kInputValues,
        kLargeActivationBytes - (65 * 10 * 32),
        kLargeActivationBytes - (33 * 5 * 48),
        kLargeActivationBytes - (17 * 3 * 64),
        kLargeActivationBytes - (9 * 2 * 96),
    };
    constexpr std::array<size_t, 4> kDepthwiseScratchCapacity = {
        0,
        kLargeActivationBytes - (65 * 10 * 32),
        kLargeActivationBytes - (33 * 5 * 48),
        kLargeActivationBytes - (17 * 3 * 64),
    };
    size_t scratch_bytes = 0;
    // The S3 general convolution is used for the 5x5 stem. Pointwise layers
    // call ESP-NN's corrected transpose/QACC 1x1 implementation directly;
    // the generic dispatcher still selects its older mult8 assembly path.
    for (size_t index = 0; index < impl->convs.size(); ++index) {
        const auto &op = impl->convs[index];
        const int bytes = index == 0
            ? esp_nn_get_conv_scratch_size(
                  &op.input_dims, &op.filter_dims, &op.output_dims,
                  &op.params)
            : static_cast<int>(((op.input_dims.channels + 7) / 8) * 128 +
                               (kAlignment - 1));
        const size_t workspace_bytes = bytes < 0 ? 0 :
            static_cast<size_t>(bytes);
        if (bytes < 0 || workspace_bytes > kConvScratchCapacity[index]) {
            ESP_LOGE(TAG,
                     "init failed: conv%u workspace=%u capacity=%u",
                     static_cast<unsigned>(index),
                     static_cast<unsigned>(workspace_bytes),
                     static_cast<unsigned>(kConvScratchCapacity[index]));
            return false;
        }
        scratch_bytes = std::max(scratch_bytes, workspace_bytes);
    }
    // Depthwise layers are a small fraction of this graph. Keep them on the
    // deterministic no-scratch path so they do not inherit SIMD register or
    // workspace state from the directly-called pointwise kernel.
    impl->scratch_bytes = scratch_bytes;

    impl_ = impl;
    arena_used_bytes_ = cursor.used();
    input_scale_ = tensor_scale(graph->tensors()->Get(0));
    input_zero_point_ = tensor_zero_point(graph->tensors()->Get(0));
    ESP_LOGI(TAG,
             "fixed verifier graph ready: arena_used=%u scratch_peak=%u "
             "scratch=activation-reuse kernels=mixed-esp-nn-aot",
             static_cast<unsigned>(arena_used_bytes_),
             static_cast<unsigned>(impl->scratch_bytes));
    if (!(input_scale_ > 0.0f)) {
        ESP_LOGE(TAG, "init failed: invalid input scale");
        impl_ = nullptr;
        return false;
    }
    return true;
}

int8_t *KizzVerifierAot::input_data() {
    return impl_ ? impl_->small : nullptr;
}

float KizzVerifierAot::output_scale() const {
    return impl_ ? impl_->output_scale : 0.0f;
}

int32_t KizzVerifierAot::output_zero_point() const {
    return impl_ ? impl_->output_zero : 0;
}

bool KizzVerifierAot::invoke(float *logit) {
    if (!impl_ || !logit) return false;
    const bool trace = !impl_->traced;
    if (trace) trace_tensor("input", impl_->small, kInputValues);
    run_conv(impl_->convs[0], impl_->small, impl_->large,
             impl_->small + kInputValues, false);
    if (trace) trace_tensor("conv0", impl_->large, 130 * 20 * 24);
    run_depthwise_ansi(impl_->depthwise[0], impl_->large, impl_->small);
    if (trace) trace_tensor("dw0", impl_->small, 65 * 10 * 24);
    run_conv_1x1(impl_->convs[1], impl_->small, impl_->large,
                 impl_->large + (65 * 10 * 32));
    if (trace) trace_tensor("conv1", impl_->large, 65 * 10 * 32);
    run_depthwise_ansi(impl_->depthwise[1], impl_->large, impl_->small);
    if (trace) trace_tensor("dw1", impl_->small, 33 * 5 * 32);
    run_conv_1x1(impl_->convs[2], impl_->small, impl_->large,
                 impl_->large + (33 * 5 * 48));
    if (trace) trace_tensor("conv2", impl_->large, 33 * 5 * 48);
    run_depthwise_ansi(impl_->depthwise[2], impl_->large, impl_->small);
    if (trace) trace_tensor("dw2", impl_->small, 17 * 3 * 48);
    run_conv_1x1(impl_->convs[3], impl_->small, impl_->large,
                 impl_->large + (17 * 3 * 64));
    if (trace) trace_tensor("conv3", impl_->large, 17 * 3 * 64);
    run_depthwise_ansi(impl_->depthwise[3], impl_->large, impl_->small);
    if (trace) trace_tensor("dw3", impl_->small, 9 * 2 * 64);
    run_conv_1x1(impl_->convs[4], impl_->small, impl_->large,
                 impl_->large + (9 * 2 * 96));
    if (trace) trace_tensor("conv4", impl_->large, 9 * 2 * 96);

    int8_t dense_quantized = 0;
    run_fully_connected_reference(
        impl_->large, impl_->dense_weights, impl_->dense_bias,
        -impl_->dense_input_zero, -impl_->dense_filter_zero,
        impl_->dense_output_zero, impl_->dense_multiplier,
        impl_->dense_shift, &dense_quantized);
    const float dense_real =
        (static_cast<int32_t>(dense_quantized) -
         impl_->dense_output_zero) * impl_->dense_output_scale;
    const int8_t tanh_quantized = quantize_int8(
        std::tanh(dense_real), impl_->tanh_output_scale,
        impl_->tanh_output_zero);
    const float tanh_real =
        (static_cast<int32_t>(tanh_quantized) -
         impl_->tanh_output_zero) * impl_->tanh_output_scale;
    const int8_t output_quantized = quantize_int8(
        tanh_real * impl_->multiply_constant, impl_->output_scale,
        impl_->output_zero);
    *logit = (static_cast<int32_t>(output_quantized) - impl_->output_zero) *
             impl_->output_scale;
    if (trace) {
        ESP_LOGI(TAG,
                 "AOT_TRACE output_q=%ld logit=%.6f",
                 static_cast<long>(output_quantized),
                 static_cast<double>(*logit));
        impl_->traced = true;
    }
    return true;
}
