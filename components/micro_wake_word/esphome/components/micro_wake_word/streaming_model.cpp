#ifdef USE_ESP_IDF

#include "esphome/components/micro_wake_word/streaming_model.h"

#include "esp_log.h"
#include "esphome/core/helpers.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

static const char *const TAG = "micro_wake_word";

namespace esphome {
namespace micro_wake_word {

void WakeWordModel::log_model_config() {
  ESP_LOGI(TAG, "    - Wake Word: %s", this->wake_word_.c_str());
  ESP_LOGI(TAG, "      Probability cutoff: %.3f", this->probability_cutoff_);
  ESP_LOGI(TAG, "      Sliding window size: %d", this->sliding_window_size_);
}

void VADModel::log_model_config() {
  ESP_LOGI(TAG, "    - VAD Model");
  ESP_LOGI(TAG, "      Probability cutoff: %.3f", this->probability_cutoff_);
  ESP_LOGI(TAG, "      Sliding window size: %d", this->sliding_window_size_);
}

bool StreamingModel::load_model(
    tflite::MicroMutableOpResolver<20> &op_resolver) {
  ExternalRAMAllocator<uint8_t> arena_allocator(
      ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);

  if (this->tensor_arena_ == nullptr) {
    this->tensor_arena_ = arena_allocator.allocate(this->tensor_arena_size_);
    if (this->tensor_arena_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate the streaming model's tensor arena.");
      return false;
    }
  }

  if (this->var_arena_ == nullptr) {
    this->var_arena_ =
        arena_allocator.allocate(STREAMING_MODEL_VARIABLE_ARENA_SIZE);
    if (this->var_arena_ == nullptr) {
      ESP_LOGE(
          TAG,
          "Could not allocate the streaming model's variable tensor arena.");
      return false;
    }
    this->ma_ = tflite::MicroAllocator::Create(
        this->var_arena_, STREAMING_MODEL_VARIABLE_ARENA_SIZE);
    this->mrv_ = tflite::MicroResourceVariables::Create(this->ma_, 20);
  }

  const tflite::Model *model = tflite::GetModel(this->model_start_);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Streaming model's schema is not supported");
    return false;
  }

  if (this->interpreter_ == nullptr) {
    this->interpreter_ = make_unique<tflite::MicroInterpreter>(
        tflite::GetModel(this->model_start_), op_resolver, this->tensor_arena_,
        this->tensor_arena_size_, this->mrv_);
    if (this->interpreter_->AllocateTensors() != kTfLiteOk) {
      ESP_LOGE(TAG, "Failed to allocate tensors for the streaming model");
      return false;
    }

    // Verify input tensor matches expected values
    // Dimension 3 will represent the first layer stride, so skip it may vary
    TfLiteTensor *input = this->interpreter_->input(0);
    if ((input->dims->size != 3) || (input->dims->data[0] != 1) ||
        (input->dims->data[2] != PREPROCESSOR_FEATURE_SIZE)) {
      ESP_LOGE(
          TAG,
          "Streaming model tensor input dimensions has improper dimensions.");
      return false;
    }

    if (input->type != kTfLiteInt8) {
      ESP_LOGE(TAG, "Streaming model tensor input is not int8.");
      return false;
    }

    // Verify output tensor matches expected values
    TfLiteTensor *output = this->interpreter_->output(0);
    const bool scalar_output =
        output->dims->size == 2 && output->dims->data[0] == 1 &&
        output->dims->data[1] == 1;
    const bool ordered_state_output =
        output->dims->size == 3 && output->dims->data[0] == 1 &&
        output->dims->data[1] == 1 && output->dims->data[2] == 23;
    if (!scalar_output && !ordered_state_output) {
      ESP_LOGE(TAG, "Streaming model tensor output dimension is not 1x1 or 1x1x23.");
    }

    if (output->type != kTfLiteUInt8) {
      ESP_LOGE(TAG, "Streaming model tensor output is not uint8.");
      return false;
    }
  }

  ESP_LOGI(TAG, "Actual tensor arena size is %d", this->interpreter_->arena_used_bytes());

  return true;
}

void StreamingModel::unload_model() {
  this->interpreter_.reset();

  ExternalRAMAllocator<uint8_t> arena_allocator(
      ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);

  arena_allocator.deallocate(this->tensor_arena_, this->tensor_arena_size_);
  this->tensor_arena_ = nullptr;
  arena_allocator.deallocate(this->var_arena_,
                             STREAMING_MODEL_VARIABLE_ARENA_SIZE);
  this->var_arena_ = nullptr;
}

bool StreamingModel::perform_streaming_inference(
    const int8_t features[PREPROCESSOR_FEATURE_SIZE]) {
  if (this->interpreter_ != nullptr) {
    TfLiteTensor *input = this->interpreter_->input(0);

    std::memmove((int8_t *)(tflite::GetTensorData<int8_t>(input)) +
                     PREPROCESSOR_FEATURE_SIZE * this->current_stride_step_,
                 features, PREPROCESSOR_FEATURE_SIZE);
    ++this->current_stride_step_;

    uint8_t stride = this->interpreter_->input(0)->dims->data[1];

    if (this->current_stride_step_ >= stride) {
      this->current_stride_step_ = 0;

      TfLiteStatus invoke_status = this->interpreter_->Invoke();
      if (invoke_status != kTfLiteOk) {
        ESP_LOGW(TAG, "Streaming interpreter invoke failed");
        return false;
      }

      TfLiteTensor *output = this->interpreter_->output(0);

      ++this->last_n_index_;
      if (this->last_n_index_ == this->sliding_window_size_)
        this->last_n_index_ = 0;
      this->recent_streaming_probabilities_[this->last_n_index_] =
          output->data.uint8[0];  // probability;
    }
    return true;
  }
  ESP_LOGE(TAG, "Streaming interpreter is not initialized.");
  return false;
}

void StreamingModel::reset_probabilities() {
  for (auto &prob : this->recent_streaming_probabilities_) {
    prob = 0;
  }
}

float StreamingModel::current_probability() const {
  if (this->recent_streaming_probabilities_.empty()) return 0.0f;
  uint32_t sum = 0;
  for (const auto probability : this->recent_streaming_probabilities_)
    sum += probability;
  return static_cast<float>(sum) /
         static_cast<float>(255 * this->recent_streaming_probabilities_.size());
}

WakeWordModel::WakeWordModel(const uint8_t *model_start,
                             float probability_cutoff,
                             size_t sliding_window_average_size,
                             const std::string &wake_word,
                             size_t tensor_arena_size) {
  this->model_start_ = model_start;
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_average_size;
  this->recent_streaming_probabilities_.resize(sliding_window_average_size, 0);
  this->wake_word_ = wake_word;
  this->tensor_arena_size_ = tensor_arena_size;
};

bool WakeWordModel::determine_detected() {
  const float sliding_window_average = this->current_probability();

  // Detect the wake word if the sliding window average is above the cutoff
  if (sliding_window_average > this->probability_cutoff_) {
    ESP_LOGD(
        TAG,
        "The '%s' model sliding average probability is %.3f and most recent "
        "probability is %.3f",
        this->wake_word_.c_str(), sliding_window_average,
        this->recent_streaming_probabilities_[this->last_n_index_] / (255.0));
    return true;
  }
  return false;
}

void WakeWordModel::set_detection_parameters(float probability_cutoff,
                                              size_t sliding_window_size) {
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_size;
  this->recent_streaming_probabilities_.assign(sliding_window_size, 0);
  this->last_n_index_ = 0;
}

VADModel::VADModel(const uint8_t *model_start, float probability_cutoff,
                   size_t sliding_window_size, size_t tensor_arena_size) {
  this->model_start_ = model_start;
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_size;
  this->recent_streaming_probabilities_.resize(sliding_window_size, 0);
  this->tensor_arena_size_ = tensor_arena_size;
};

bool VADModel::determine_detected() {
  uint32_t sum = 0;
  for (auto &prob : this->recent_streaming_probabilities_) {
    sum += prob;
  }

  float sliding_window_average =
      static_cast<float>(sum) /
      static_cast<float>(255 * this->sliding_window_size_);

  return sliding_window_average > this->probability_cutoff_;
}

OrderedStateModel::OrderedStateModel(const uint8_t *model_start,
                                     float probability_cutoff,
                                     size_t sliding_window_size,
                                     const std::string &wake_word,
                                     size_t tensor_arena_size) {
  this->model_start_ = model_start;
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_size;
  this->tensor_arena_size_ = tensor_arena_size;
  this->wake_word_ = wake_word;
  this->recent_streaming_probabilities_.resize(1, 0);
  this->reset_decoder();
  this->set_detection_parameters(probability_cutoff, sliding_window_size);
}

void OrderedStateModel::log_model_config() {
  ESP_LOGI(TAG, "    - Ordered Wake Word: %s", this->wake_word_.c_str());
  ESP_LOGI(TAG, "      Completion margin: %.3f (display cutoff %.3f)",
           static_cast<double>(this->ordered_score_cutoff_),
           static_cast<double>(this->probability_cutoff_));
}

void OrderedStateModel::set_detection_parameters(float probability_cutoff,
                                                 size_t sliding_window_size) {
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_size;
  const float clipped = std::max(0.001f, std::min(0.999f, probability_cutoff));
  const float logit = std::log(clipped / (1.0f - clipped));
  const float baseline_logit = std::log(
      this->baseline_probability_cutoff_ /
      (1.0f - this->baseline_probability_cutoff_));
  this->ordered_score_cutoff_ = this->baseline_score_ + logit - baseline_logit;
}

void OrderedStateModel::reset_decoder() {
  for (float &score : this->scores_) score = -INFINITY;
  this->completion_score_ = -INFINITY;
}

float OrderedStateModel::ordered_probability() const {
  if (!std::isfinite(this->completion_score_)) return 0.0f;
  const float baseline_logit = std::log(
      this->baseline_probability_cutoff_ /
      (1.0f - this->baseline_probability_cutoff_));
  const float shifted = this->completion_score_ - this->baseline_score_ +
                        baseline_logit;
  if (shifted >= 40.0f) return 1.0f;
  if (shifted <= -40.0f) return 0.0f;
  return 1.0f / (1.0f + std::exp(-shifted));
}

bool OrderedStateModel::perform_ordered_inference(
    const int8_t features[PREPROCESSOR_FEATURE_SIZE]) {
  const uint8_t stride = this->interpreter_ == nullptr
                             ? 0
                             : this->interpreter_->input(0)->dims->data[1];
  const bool emits_frame = stride > 0 &&
                           this->current_stride_step_ + 1 >= stride;
  if (!this->perform_streaming_inference(features)) return false;
  if (!emits_frame) return true;

  const TfLiteTensor *output = this->interpreter_->output(0);
  if (output->type != kTfLiteUInt8 || output->bytes < 23) return false;
  float logits[23];
  float max_logit = -FLT_MAX;
  for (size_t index = 0; index < 23; ++index) {
    logits[index] = (static_cast<float>(output->data.uint8[index]) -
                     static_cast<float>(output->params.zero_point)) *
                    output->params.scale;
    max_logit = std::max(max_logit, logits[index]);
  }
  float normalizer = 0.0f;
  for (float logit : logits) normalizer += std::exp(logit - max_logit);
  const float log_normalizer = max_logit + std::log(normalizer);
  const float log_background = logits[0] - log_normalizer;
  const float log_silence = logits[1] - log_normalizer;
  const float rejection_max = std::max(log_background, log_silence);
  const float rejection = rejection_max +
                          std::log(std::exp(log_background - rejection_max) +
                                   std::exp(log_silence - rejection_max));

  float next_scores[21];
  for (size_t state = 0; state < 21; ++state) {
    const float emission = logits[state + 2] - log_normalizer - rejection;
    float best = -INFINITY;
    if (state == 0) best = emission;
    if (std::isfinite(this->scores_[state]))
      best = std::max(best, this->scores_[state] + this->log_self_ + emission);
    if (state > 0 && std::isfinite(this->scores_[state - 1]))
      best = std::max(best, this->scores_[state - 1] + this->log_next_ + emission);
    next_scores[state] = best;
  }
  for (size_t state = 0; state < 21; ++state)
    this->scores_[state] = next_scores[state];
  this->completion_score_ = this->scores_[20];
  return true;
}

bool OrderedStateModel::determine_detected() {
  return std::isfinite(this->completion_score_) &&
         this->completion_score_ >= this->ordered_score_cutoff_;
}

}  // namespace micro_wake_word
}  // namespace esphome

#endif
