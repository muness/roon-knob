#pragma once

#include <cstddef>
#include <cstdint>

namespace tflite {
class Model;
}

// Model-specific fixed executor for the Kizz fixed-window verifier. The graph
// schedule and memory plan are compiled into firmware while weights and
// quantization metadata remain bound to the embedded TFLite artifact.
class KizzVerifierAot final {
 public:
    struct Impl;

    static constexpr size_t kInputFrames = 260;
    static constexpr size_t kFeatureBins = 40;
    static constexpr size_t kInputValues = kInputFrames * kFeatureBins;

    bool initialize(const tflite::Model *model, uint8_t *arena,
                    size_t arena_bytes);
    int8_t *input_data();
    bool invoke(float *logit);

    bool initialized() const { return impl_ != nullptr; }
    size_t arena_used_bytes() const { return arena_used_bytes_; }
    float input_scale() const { return input_scale_; }
    int32_t input_zero_point() const { return input_zero_point_; }
    float output_scale() const;
    int32_t output_zero_point() const;

 private:
    Impl *impl_{nullptr};
    size_t arena_used_bytes_{0};
    float input_scale_{0.0f};
    int32_t input_zero_point_{0};
};
