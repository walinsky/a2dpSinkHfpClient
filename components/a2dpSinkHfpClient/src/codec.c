#include "codec.h"
#include "esp_log.h"
#include "esp_sbc_enc.h"
#include "esp_sbc_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CODEC";

// mSBC Configuration Constants
#define MSBC_SAMPLE_RATE 16000
#define MSBC_CHANNELS 1
#define MSBC_BITS_PER_SAMPLE 16
#define MSBC_FRAME_SIZE_BYTES (MSBC_FRAME_SAMPLES * 2)  // 240 bytes

// Static buffers to avoid fragmentation during encoding
static int16_t s_enc_input_buf[MSBC_FRAME_SAMPLES];
static uint8_t s_enc_output_buf[MSBC_ENCODED_SIZE];

// Static handles for encoder and decoder
static void *encoder_handle = NULL;
static void *decoder_handle = NULL;
static void *a2dp_decoder_handle = NULL;

// Encoder serialization mutex (thread-safe access)
static SemaphoreHandle_t s_encoder_mutex = NULL;

static uint16_t s_enc_frame_count = 0;
static uint8_t s_skip_first_enc_call = 1;

int msbc_enc_open(void)
{
    if (encoder_handle != NULL) {
        ESP_LOGW(TAG, "Encoder already open");
        return 0;
    }

    // Create mutex once for thread-safe encoder access
    if (s_encoder_mutex == NULL) {
        s_encoder_mutex = xSemaphoreCreateMutex();
        if (s_encoder_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create encoder mutex");
            return -1;
        }
    }

#ifdef ESP_SBC_MSBC_ENC_CONFIG_DEFAULT
    esp_sbc_enc_config_t enc_cfg = ESP_SBC_MSBC_ENC_CONFIG_DEFAULT();
#else
    esp_sbc_enc_config_t enc_cfg = {
        .sbc_mode = ESP_SBC_MODE_MSBC,
        .allocation_method = ESP_SBC_AM_LOUDNESS,
        .ch_mode = ESP_SBC_CH_MODE_MONO,
        .sample_rate = MSBC_SAMPLE_RATE,
        .bits_per_sample = MSBC_BITS_PER_SAMPLE,
        .bitpool = 26,
        .block_length = 15,
        .sub_bands_num = 8,
    };
#endif

    int ret = esp_sbc_enc_open(&enc_cfg, sizeof(esp_sbc_enc_config_t), &encoder_handle);
    if (ret != 0 || encoder_handle == NULL) {
        ESP_LOGE(TAG, "Failed to open mSBC encoder, error: %d", ret);
        encoder_handle = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "mSBC encoder opened successfully");
    return 0;
}

void msbc_enc_close(void)
{
    if (encoder_handle != NULL) {
        esp_sbc_enc_close(encoder_handle);
        encoder_handle = NULL;
        ESP_LOGI(TAG, "mSBC encoder closed");
    }
}

int msbc_dec_open(void)
{
    if (decoder_handle != NULL) {
        ESP_LOGW(TAG, "Decoder already open");
        return 0;
    }

    esp_sbc_dec_cfg_t dec_cfg = {
        .sbc_mode = ESP_SBC_MODE_MSBC,
        .ch_num = MSBC_CHANNELS,
        .enable_plc = 1,
    };

    ESP_LOGI(TAG, "Opening decoder with: mode=%d, ch_num=%d, plc=%d",
            dec_cfg.sbc_mode, dec_cfg.ch_num, dec_cfg.enable_plc);

    int ret = esp_sbc_dec_open(&dec_cfg, sizeof(esp_sbc_dec_cfg_t), &decoder_handle);
    if (ret != 0 || decoder_handle == NULL) {
        ESP_LOGE(TAG, "Failed to open mSBC decoder, error: %d", ret);
        decoder_handle = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "mSBC decoder opened successfully");
    return 0;
}

void msbc_dec_close(void)
{
    if (decoder_handle != NULL) {
        esp_sbc_dec_close(decoder_handle);
        decoder_handle = NULL;
        ESP_LOGI(TAG, "mSBC decoder closed");
    }
}

int msbc_enc_data(const uint8_t *in_data, size_t in_data_len,
                  uint8_t *out_data, size_t *out_data_len)
{
    // I filed a bug report at https://github.com/espressif/esp-adf/issues/1553
    // once this get resolved we can actually encode
    // for now we simply don't; and return -1
    return -1;
    // Validate input parameters
    if (in_data == NULL || out_data == NULL || out_data_len == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for encoding");
        return -1;
    }

    // Encoder must be open
    if (encoder_handle == NULL) {
        ESP_LOGW(TAG, "Encoder not initialized");
        return -1;
    }

    // Verify we're getting 120 samples (240 bytes for 16-bit stereo)
    if (in_data_len != MSBC_FRAME_SIZE_BYTES) {
        ESP_LOGW(TAG, "Input data length mismatch: got %zu, expected %d", 
                 in_data_len, MSBC_FRAME_SIZE_BYTES);
        return -1;
    }

    // Skip the first call for the HFP RX task
    if (s_skip_first_enc_call) {
        *out_data_len = 0;
        s_skip_first_enc_call = 0;
        return 0;
    }

    // Mutex MUST exist (created in msbc_enc_open)
    if (s_encoder_mutex == NULL) {
        ESP_LOGE(TAG, "Encoder mutex not initialized - msbc_enc_open() was not called");
        return -1;
    }

    // Lock encoder for thread-safe access
    if (xSemaphoreTake(s_encoder_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Encoder mutex timeout");
        return -1;
    }

    // Copy input to static buffer
    memcpy(s_enc_input_buf, in_data, in_data_len);

    // Prepare input frame
    esp_audio_enc_in_frame_t in_frame = {0};
    in_frame.buffer = (uint8_t *)s_enc_input_buf;
    in_frame.len = in_data_len;

    // Prepare output frame
    esp_audio_enc_out_frame_t out_frame = {0};
    out_frame.buffer = s_enc_output_buf;
    out_frame.len = MSBC_ENCODED_SIZE;

    // Encode the data
    int ret = esp_sbc_enc_process(encoder_handle, &in_frame, &out_frame);

    // Unlock encoder
    xSemaphoreGive(s_encoder_mutex);

    // Check if encoding was successful
    if (ret != 0) {
        ESP_LOGE(TAG, "Encoding failed: %d", ret);
        *out_data_len = 0;
        return -1;
    }

    // Copy encoded data to output
    if (out_frame.len > 0 && out_frame.len <= MSBC_ENCODED_SIZE) {
        memcpy(out_data, s_enc_output_buf, out_frame.len);
        *out_data_len = out_frame.len;
    } else {
        ESP_LOGW(TAG, "Encoded frame size invalid: %d", out_frame.len);
        *out_data_len = 0;
        ret = -1;
    }

    return ret;
}

// Reset encoder frame count when HFP disconnects
void msbc_enc_reset_frame_count(void)
{
    s_enc_frame_count = 0;
}

int msbc_dec_data(const uint8_t *in_data, size_t in_data_len,
                  uint8_t *out_data, size_t *out_data_len)
{
    if (in_data == NULL || out_data == NULL || out_data_len == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for decoding");
        return -1;
    }

    if (decoder_handle == NULL) {
        ESP_LOGW(TAG, "Decoder not initialized. Call msbc_dec_open() first.");
        return -1;
    }

    // Prepare input frame
    esp_audio_dec_in_raw_t in_frame = {
        .buffer = (uint8_t *)in_data,
        .len = in_data_len,
    };

    // Prepare output frame
    esp_audio_dec_out_frame_t out_frame = {
        .buffer = out_data,
        .len = MSBC_FRAME_SIZE_BYTES * 2,
        .decoded_size = 0,
    };

    // Prepare info structure
    esp_audio_dec_info_t dec_info = {0};

    // Decode the data
    int ret = esp_sbc_dec_decode(decoder_handle, &in_frame, &out_frame, &dec_info);

    if (ret != 0) {
        ESP_LOGE(TAG, "Decoding failed, error: %d", ret);
        return -1;
    }

    *out_data_len = out_frame.decoded_size;
    ESP_LOGD(TAG, "Decoded %zu bytes to %d bytes", in_data_len, out_frame.decoded_size);
    return 0;
}

int a2dp_sbc_dec_open(int sample_rate, int channels)
{
    if (a2dp_decoder_handle != NULL) {
        ESP_LOGW(TAG, "A2DP decoder already open");
        return 0;
    }

    /* A2DP uses standard SBC, not mSBC */
    esp_sbc_dec_cfg_t dec_cfg = {
        .sbc_mode = ESP_SBC_MODE_STD,
        .ch_num = channels,
        .enable_plc = 1,
    };

    int ret = esp_sbc_dec_open(&dec_cfg, sizeof(esp_sbc_dec_cfg_t), &a2dp_decoder_handle);
    if (ret != 0 || a2dp_decoder_handle == NULL) {
        ESP_LOGE(TAG, "Failed to open A2DP SBC decoder, error: %d", ret);
        a2dp_decoder_handle = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "A2DP SBC decoder opened (sr=%d, ch=%d)", sample_rate, channels);
    return 0;
}

void a2dp_sbc_dec_close(void)
{
    if (a2dp_decoder_handle != NULL) {
        esp_sbc_dec_close(a2dp_decoder_handle);
        a2dp_decoder_handle = NULL;
        ESP_LOGI(TAG, "A2DP SBC decoder closed");
    }
}

/**
 * @brief Decode SBC and return bytes consumed + output length
 * Call this function repeatedly until all data is consumed
 */
int a2dp_sbc_dec_data(const uint8_t *in_data, size_t in_data_len,
                      uint8_t *out_data, size_t *out_data_len,
                      size_t *in_bytes_consumed)
{
    if (in_data == NULL || out_data == NULL || out_data_len == NULL || in_bytes_consumed == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    if (a2dp_decoder_handle == NULL) {
        ESP_LOGW(TAG, "A2DP decoder not initialized");
        return -1;
    }

    esp_audio_dec_in_raw_t in_frame = {
        .buffer = (uint8_t *)in_data,
        .len = in_data_len,
        .consumed = 0,
        .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
    };

    esp_audio_dec_out_frame_t out_frame = {
        .buffer = out_data,
        .len = 2048,
        .decoded_size = 0,
    };

    /* ALLOCATE INFO STRUCTURE */
    esp_audio_dec_info_t dec_info = {0};

    /* Pass valid pointer, not NULL */
    int ret = esp_sbc_dec_decode(a2dp_decoder_handle, &in_frame, &out_frame, &dec_info);
    
    *out_data_len = out_frame.decoded_size;
    *in_bytes_consumed = in_frame.consumed;
    
    if (ret != 0) {
        /* Only log real issues, not sync errors */
        // if (ret != -3 && ret != -1) {  /* -3 = data lack, -1 = sync fail, both normal */
        //     ESP_LOGE(TAG, "A2DP decoding failed: %d", ret);
        // }
        return ret;
    }

    return 0;
}

void i2s_32bit_to_16bit_pcm(const int32_t *i2s_data, uint8_t *pcm_data, size_t num_samples)
{
    uint8_t *input_bytes = (uint8_t *)i2s_data;
    for (size_t i = 0; i < num_samples; i++) {
        // Extract bytes [2] and [3] from each 32-bit word
        pcm_data[i * 2] = input_bytes[i * 4 + 2];     // mid byte
        pcm_data[i * 2 + 1] = input_bytes[i * 4 + 3]; // high byte
    }
}
