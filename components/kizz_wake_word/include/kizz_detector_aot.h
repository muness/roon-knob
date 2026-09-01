#pragma once

#include <cstddef>
#include <cstdint>

namespace tflite {
class Model;
}

// Model-specific ahead-of-time executor for the qualified Kizz Control v5c
// detector. The execution schedule is compiled into firmware; the immutable
// weights and quantization metadata remain provenance-bound to the embedded
// TFLite artifact.
class KizzDetectorAot final {
 public:
    struct Impl;

    static constexpr size_t kInputFrames = 3;
    static constexpr size_t kFeatureBins = 40;
    static constexpr size_t kInputValues = kInputFrames * kFeatureBins;
    static constexpr size_t kOutputValues = 12;

    bool initialize(const tflite::Model *model, uint8_t *arena,
                    size_t arena_bytes);
    void reset();
    bool invoke(const int8_t input[kInputValues],
                uint8_t output[kOutputValues]);

    bool initialized() const { return impl_ != nullptr; }
    size_t arena_used_bytes() const { return arena_used_bytes_; }
    size_t scratch_bytes() const { return scratch_bytes_; }
    float input_scale() const { return input_scale_; }
    int8_t input_zero_point() const { return input_zero_point_; }
    float output_scale() const { return output_scale_; }
    int32_t output_zero_point() const { return output_zero_point_; }

 private:
    Impl *impl_{nullptr};
    size_t arena_used_bytes_{0};
    size_t scratch_bytes_{0};
    float input_scale_{0.0f};
    int8_t input_zero_point_{-128};
    float output_scale_{0.0f};
    int32_t output_zero_point_{0};
};
