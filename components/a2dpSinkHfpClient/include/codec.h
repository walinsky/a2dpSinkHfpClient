#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>
#include <stddef.h>

#define MSBC_FRAME_SAMPLES      120  // mSBC uses 120 samples per frame
#define MSBC_ENCODED_SIZE       120  // or ESP_HF_MSBC_ENCODED_FRAME_SIZE (57)?

/**
 * @brief Initialize and open the mSBC encoder
 * 
 * @return 0 on success, -1 on failure
 */
int msbc_enc_open(void);

/**
 * @brief Close the mSBC encoder and free resources
 */
void msbc_enc_close(void);

/**
 * @brief Initialize and open the mSBC decoder
 * 
 * @return 0 on success, -1 on failure
 */
int msbc_dec_open(void);

/**
 * @brief Close the mSBC decoder and free resources
 */
void msbc_dec_close(void);

/**
 * @brief Encode PCM audio data to mSBC format
 * 
 * @param in_data Pointer to input PCM data buffer (16-bit samples, mono, 16kHz)
 * @param in_data_len Length of input data in bytes (should be 480 bytes = 240 samples)
 * @param out_data Pointer to output buffer for encoded mSBC data (minimum 120 bytes)
 * @param out_data_len Pointer to variable that will receive the output data length
 * 
 * @return 0 on success, -1 on failure
 */
int msbc_enc_data(const uint8_t *in_data, size_t in_data_len, 
                  uint8_t *out_data, size_t *out_data_len);

/**
 * @brief Decode mSBC data to PCM audio format
 * 
 * @param in_data Pointer to input mSBC encoded data buffer
 * @param in_data_len Length of input data in bytes
 * @param out_data Pointer to output buffer for decoded PCM data (minimum 960 bytes)
 * @param out_data_len Pointer to variable that will receive the output data length
 * 
 * @return 0 on success, -1 on failure
 */
int msbc_dec_data(const uint8_t *in_data, size_t in_data_len, 
                  uint8_t *out_data, size_t *out_data_len);

int a2dp_sbc_dec_open(int sample_rate, int channels);
void a2dp_sbc_dec_close(void);
int a2dp_sbc_dec_data(const uint8_t *in_data, size_t in_data_len,
                      uint8_t *out_data, size_t *out_data_len,
                      size_t *in_bytes_consumed);

/**
 * @brief Convert 32-bit I2S data from INMP441 to 16-bit PCM
 * 
 * @param i2s_data Input buffer with 32-bit I2S data
 * @param pcm_data Output buffer for 16-bit PCM data
 * @param num_samples Number of samples to convert
 */
void i2s_32bit_to_16bit_pcm(const int32_t *i2s_data, uint8_t *pcm_data, size_t num_samples);

void msbc_enc_reset_frame_count(void);

#endif // CODEC_H
