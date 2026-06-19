#include "wakeword.h"
#include "mic.h"
#include "esp_log.h"

#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esphome/components/microphone/microphone.h"

#include <string>

// Embedded "Hello Computer" TFLite model: hello_computer_tflite[] / _len.
// Community model from github.com/TaterTotterson/microWakeWords (microWakeWordsV3).
#include "hello_computer_model.h"

static const char *TAG = "wakeword";

// Per-model tuning straight from the model's published .json metadata (the values
// it was trained/validated with), so they live here paired with this model.
// hello_computer.json: probability_cutoff 0.53, sliding_window_size 3,
// feature_step_size 10, tensor_arena_size 30000.
#define WW_PROBABILITY_CUTOFF   0.53f
#define WW_SLIDING_WINDOW       3
#define WW_FEATURES_STEP_SIZE   10
// Arena: the model's json asks for 30000; use 48 KB with headroom (allocated from
// PSRAM, so oversizing is cheap insurance against a silent voice-disable).
#define WW_TENSOR_ARENA_SIZE    (48 * 1024)

// Latch set from the detection callback, drained edge-triggered by
// wakeword_detected(). volatile: written from the component's loop() context,
// read on the same core-0 tick but kept honest against the optimizer.
static volatile bool s_detected = false;

// Adapts the board mic (mic.cpp / esp_codec_dev) to the esphome Microphone
// interface the wake-word component reads through. start()/stop() only flip the
// state_ the component polls via is_running()/is_stopped(); the codec itself is
// opened once in wakeword_init() and left running (cheap, and re-opening on
// every one-shot re-arm would add latency).
class BspMicrophone : public esphome::microphone::Microphone {
 public:
    void start() override { this->state_ = esphome::microphone::STATE_RUNNING; }
    void stop() override { this->state_ = esphome::microphone::STATE_STOPPED; }

    // The component calls read(buf, len) with len in BYTES and treats the return
    // value as BYTES read (verified against MicroWakeWord::read_microphone_(),
    // which passes INPUT_BUFFER_SIZE * sizeof(int16_t) and the bundled
    // I2SAudioMicrophone's 16-bit path returns bytes_read). mic_read works in
    // samples, so convert in and out.
    size_t read(int16_t *buf, size_t len) override {
        size_t samples = len / sizeof(int16_t);
        if (samples == 0) {
            return 0;
        }
        size_t got = mic_read(buf, samples);
        return got * sizeof(int16_t);
    }
};

static BspMicrophone s_mic;
static esphome::micro_wake_word::MicroWakeWord s_ww;

static bool s_armed = true;

int wakeword_init(void)
{
    if (mic_init() != 0) {
        ESP_LOGE(TAG, "mic_init failed; voice disabled");
        return -1;
    }

    s_ww.set_microphone(&s_mic);
    s_ww.add_wake_word_model(hello_computer_tflite, WW_PROBABILITY_CUTOFF,
                             WW_SLIDING_WINDOW, "Hello Computer",
                             WW_TENSOR_ARENA_SIZE);
    s_ww.set_features_step_size(WW_FEATURES_STEP_SIZE);
    s_ww.add_detection_callback([](std::string) { s_detected = true; });

    s_ww.setup();
    s_ww.start();

    if (!s_ww.is_running()) {
        ESP_LOGE(TAG, "micro_wake_word failed to start (model/arena/buffers)");
        return -1;
    }

    ESP_LOGI(TAG, "wake-word detector running");
    return 0;
}

void wakeword_update(void)
{
    if (!s_armed) {
        return;   // mic owned by the recorder; don't run the detector or re-arm
    }
    s_ww.loop();
    // micro_wake_word is a one-shot detector: on a hit it stops the mic and
    // returns to IDLE. Re-arm so we keep listening for the next phrase.
    if (!s_ww.is_running()) {
        s_ww.start();
    }
}

bool wakeword_detected(void)
{
    if (s_detected) {
        s_detected = false;
        return true;
    }
    return false;
}

bool wakeword_speech_active(void)
{
    // This component is wake-word only (no VAD); it gives no post-wake speech
    // signal. The FSM uses a fixed PONDER_MS instead.
    return false;
}

void wakeword_set_armed(bool armed)
{
    s_armed = armed;
    if (armed && !s_ww.is_running()) {
        s_ww.start();
    }
}
