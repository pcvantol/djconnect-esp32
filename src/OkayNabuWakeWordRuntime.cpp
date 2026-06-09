// Local Okay Nabu wake-word runtime using TensorFlow Lite Micro and the
// TensorFlow micro_speech frontend.
#include <Arduino.h>

#include <cmath>
#include <new>

#include <esp_heap_caps.h>

#include "AppLog.h"
#include "Config.h"
#include "OkayNabuWakeWordModel.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
static constexpr size_t kFeatureFramesPerInference = 3;
static constexpr size_t kFeatureBins = 40;
static constexpr size_t kMaxResourceVariables = 8;
static constexpr uint32_t kDetectionCooldownMs = 3500;
static constexpr size_t kTensorArenaBytes = kOkayNabuWakeWordTensorArenaBytes + 4096;

struct OkayNabuRuntime {
  bool initialized = false;
  bool frontendReady = false;
  bool tensorsReady = false;
  FrontendState frontend = {};
  alignas(16) uint8_t tensorArena[kTensorArenaBytes] = {};
  tflite::MicroAllocator *allocator = nullptr;
  tflite::MicroResourceVariables *resources = nullptr;
  tflite::MicroMutableOpResolver<18> resolver;
  tflite::MicroInterpreter *interpreter = nullptr;
  alignas(tflite::MicroInterpreter) uint8_t interpreterStorage[sizeof(tflite::MicroInterpreter)] = {};
  int8_t featureWindow[kFeatureFramesPerInference][kFeatureBins] = {};
  size_t pendingFeatureFrames = 0;
  float probabilities[kOkayNabuWakeWordSlidingWindowSize] = {};
  size_t probabilityCount = 0;
  size_t probabilityIndex = 0;
  uint32_t lastDetectionAt = 0;
  uint32_t lastScoreLogAt = 0;
  float bestProbabilitySinceLog = 0.0f;
  size_t outputElementCount = 0;
};

OkayNabuRuntime *gRuntime = nullptr;

int8_t quantizeFeature(uint16_t feature) {
  const int32_t quantized = static_cast<int32_t>(lroundf(
      static_cast<float>(feature) / 0.10196078568696976f - 128.0f));
  return static_cast<int8_t>(constrain(quantized, -128, 127));
}

bool addModelOps(OkayNabuRuntime &runtime) {
  return runtime.resolver.AddCallOnce() == kTfLiteOk &&
         runtime.resolver.AddVarHandle() == kTfLiteOk &&
         runtime.resolver.AddReadVariable() == kTfLiteOk &&
         runtime.resolver.AddReshape() == kTfLiteOk &&
         runtime.resolver.AddAssignVariable() == kTfLiteOk &&
         runtime.resolver.AddAdd() == kTfLiteOk &&
         runtime.resolver.AddDepthwiseConv2D() == kTfLiteOk &&
         runtime.resolver.AddConv2D() == kTfLiteOk &&
         runtime.resolver.AddMul() == kTfLiteOk &&
         runtime.resolver.AddSplit() == kTfLiteOk &&
         runtime.resolver.AddSplitV() == kTfLiteOk &&
         runtime.resolver.AddConcatenation() == kTfLiteOk &&
         runtime.resolver.AddStridedSlice() == kTfLiteOk &&
         runtime.resolver.AddFullyConnected() == kTfLiteOk &&
         runtime.resolver.AddLogistic() == kTfLiteOk &&
         runtime.resolver.AddQuantize() == kTfLiteOk &&
         runtime.resolver.AddMean() == kTfLiteOk;
}

bool setupFrontend(OkayNabuRuntime &runtime) {
  FrontendConfig config = {};
  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms = 30;
  config.window.step_size_ms = kOkayNabuWakeWordFeatureStepMs;
  config.filterbank.num_channels = kFeatureBins;
  config.filterbank.lower_band_limit = 125.0f;
  config.filterbank.upper_band_limit = 7500.0f;
  config.pcan_gain_control.enable_pcan = 1;
  config.noise_reduction.min_signal_remaining = 0.05f;
  config.log_scale.scale_shift = 0;

  runtime.frontendReady = FrontendPopulateState(&config, &runtime.frontend, Config::VoiceSampleRate) != 0;
  return runtime.frontendReady;
}

bool setupInterpreter(OkayNabuRuntime &runtime) {
  const tflite::Model *model = tflite::GetModel(kOkayNabuWakeWordModel);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION) {
    AppLog.println("Wake word: TFLite model schema mismatch");
    return false;
  }
  if (!addModelOps(runtime)) {
    AppLog.println("Wake word: TFLite op resolver failed");
    return false;
  }

  runtime.allocator = tflite::MicroAllocator::Create(runtime.tensorArena, sizeof(runtime.tensorArena));
  if (runtime.allocator == nullptr) {
    AppLog.println("Wake word: TFLite allocator failed");
    return false;
  }
  runtime.resources = tflite::MicroResourceVariables::Create(runtime.allocator, kMaxResourceVariables);
  if (runtime.resources == nullptr) {
    AppLog.println("Wake word: TFLite resource variables failed");
    return false;
  }

  runtime.interpreter = new (runtime.interpreterStorage)
      tflite::MicroInterpreter(model, runtime.resolver, runtime.allocator, runtime.resources);
  if (runtime.interpreter->AllocateTensors() != kTfLiteOk) {
    AppLog.println("Wake word: TFLite tensor allocation failed");
    return false;
  }

  TfLiteTensor *input = runtime.interpreter->input(0);
  TfLiteTensor *output = runtime.interpreter->output(0);
  size_t outputElementCount = 1;
  if (output != nullptr && output->dims != nullptr) {
    for (int index = 0; index < output->dims->size; ++index) {
      outputElementCount *= static_cast<size_t>(max(1, output->dims->data[index]));
    }
  }
  if (input == nullptr || output == nullptr ||
      input->type != kTfLiteInt8 ||
      output->type != kTfLiteUInt8 ||
      input->dims == nullptr || input->dims->size != 3 ||
      input->dims->data[0] != 1 ||
      input->dims->data[1] != static_cast<int>(kFeatureFramesPerInference) ||
      input->dims->data[2] != static_cast<int>(kFeatureBins)) {
    AppLog.println("Wake word: TFLite tensor shape unsupported");
    return false;
  }

  runtime.outputElementCount = outputElementCount;
  runtime.tensorsReady = true;
  return true;
}

bool ensureRuntime() {
  static uint32_t lastAllocationFailureLogAt = 0;
  if (gRuntime == nullptr) {
    gRuntime = new (std::nothrow) OkayNabuRuntime();
    if (gRuntime == nullptr) {
      const uint32_t now = millis();
      if (now - lastAllocationFailureLogAt > 5000) {
        AppLog.line(String("Wake word: runtime allocation failed, free=") +
                    String(heap_caps_get_free_size(MALLOC_CAP_8BIT)) +
                    ", largest=" +
                    String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        lastAllocationFailureLogAt = now;
      }
      return false;
    }
  }
  OkayNabuRuntime &runtime = *gRuntime;
  if (runtime.initialized) {
    return runtime.frontendReady && runtime.tensorsReady;
  }
  runtime.initialized = true;
  if (!setupFrontend(runtime)) {
    AppLog.println("Wake word: frontend init failed");
    return false;
  }
  if (!setupInterpreter(runtime)) {
    AppLog.println("Wake word: runtime init failed");
    return false;
  }
  AppLog.print("Wake word: Okay Nabu runtime ready, arena=");
  AppLog.println(static_cast<int>(kTensorArenaBytes));
  AppLog.print("Wake word: output elements=");
  AppLog.println(static_cast<int>(runtime.outputElementCount));
  return true;
}

bool invokeModel(OkayNabuRuntime &runtime) {
  TfLiteTensor *input = runtime.interpreter->input(0);
  int8_t *inputData = tflite::GetTensorData<int8_t>(input);
  if (inputData == nullptr) {
    return false;
  }
  memcpy(inputData, runtime.featureWindow, kFeatureFramesPerInference * kFeatureBins);

  if (runtime.interpreter->Invoke() != kTfLiteOk) {
    AppLog.println("Wake word: TFLite invoke failed");
    return false;
  }

  const TfLiteTensor *output = runtime.interpreter->output(0);
  const uint8_t *outputData = tflite::GetTensorData<uint8_t>(output);
  if (outputData == nullptr) {
    return false;
  }
  const size_t outputIndex = runtime.outputElementCount > 1 ? runtime.outputElementCount - 1 : 0;
  const float probability = static_cast<float>(outputData[outputIndex]) * 0.00390625f;
  runtime.bestProbabilitySinceLog = max(runtime.bestProbabilitySinceLog, probability);
  runtime.probabilities[runtime.probabilityIndex] = probability;
  runtime.probabilityIndex = (runtime.probabilityIndex + 1) % kOkayNabuWakeWordSlidingWindowSize;
  if (runtime.probabilityCount < kOkayNabuWakeWordSlidingWindowSize) {
    runtime.probabilityCount++;
  }

  float sum = 0.0f;
  for (size_t index = 0; index < runtime.probabilityCount; ++index) {
    sum += runtime.probabilities[index];
  }
  const float average = sum / static_cast<float>(runtime.probabilityCount);
  const uint32_t now = millis();
  if (now - runtime.lastScoreLogAt >= 3000) {
    AppLog.print("Wake word: score current=");
    AppLog.print(probability, 3);
    AppLog.print(" avg=");
    AppLog.print(average, 3);
    AppLog.print(" best=");
    AppLog.print(runtime.bestProbabilitySinceLog, 3);
    AppLog.print(" cutoff=");
    AppLog.println(kOkayNabuWakeWordProbabilityCutoff, 2);
    runtime.bestProbabilitySinceLog = 0.0f;
    runtime.lastScoreLogAt = now;
  }
  if (runtime.probabilityCount >= kOkayNabuWakeWordSlidingWindowSize &&
      average >= kOkayNabuWakeWordProbabilityCutoff &&
      now - runtime.lastDetectionAt >= kDetectionCooldownMs) {
    runtime.lastDetectionAt = now;
    runtime.probabilityCount = 0;
    runtime.probabilityIndex = 0;
    memset(runtime.probabilities, 0, sizeof(runtime.probabilities));
    AppLog.print("Wake word: Okay Nabu probability=");
    AppLog.println(average, 3);
    return true;
  }
  return false;
}

bool processFeature(OkayNabuRuntime &runtime, const FrontendOutput &output) {
  if (output.size != kFeatureBins || output.values == nullptr) {
    return false;
  }
  for (size_t index = 0; index < kFeatureBins; ++index) {
    runtime.featureWindow[runtime.pendingFeatureFrames][index] = quantizeFeature(output.values[index]);
  }
  runtime.pendingFeatureFrames++;
  if (runtime.pendingFeatureFrames < kFeatureFramesPerInference) {
    return false;
  }
  runtime.pendingFeatureFrames = 0;
  return invokeModel(runtime);
}
}  // namespace

extern "C" bool djconnect_oke_nabu_wake_word_detect(const int16_t *samples, size_t sampleCount) {
  if (samples == nullptr || sampleCount == 0 || !ensureRuntime()) {
    return false;
  }
  OkayNabuRuntime &runtime = *gRuntime;

  size_t offset = 0;
  while (offset < sampleCount) {
    size_t samplesRead = 0;
    const FrontendOutput output = FrontendProcessSamples(
        &runtime.frontend,
        samples + offset,
        sampleCount - offset,
        &samplesRead);
    if (samplesRead == 0) {
      break;
    }
    offset += samplesRead;
    if (output.size > 0 && processFeature(runtime, output)) {
      return true;
    }
  }
  return false;
}
