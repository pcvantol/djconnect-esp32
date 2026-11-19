#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint8_t kOkayNabuWakeWordModel[];
extern const size_t kOkayNabuWakeWordModelLen;

static constexpr const char *kOkayNabuWakeWordName = "Okay Nabu";
static constexpr float kOkayNabuWakeWordProbabilityCutoff = 0.90f;
static constexpr uint8_t kOkayNabuWakeWordSlidingWindowSize = 3;
static constexpr uint16_t kOkayNabuWakeWordFeatureStepMs = 10;
static constexpr size_t kOkayNabuWakeWordTensorArenaBytes = 26080;
static constexpr const char *kOkayNabuWakeWordModelSha256 =
    "0689abe1912a95a3318a0d8cb2e67bad0cbcfe3e24dd6e050c75debddfb6f891";
