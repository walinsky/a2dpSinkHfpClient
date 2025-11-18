/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef __BT_I2S_H__
#define __BT_I2S_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/i2s_std.h"

/**
 * @brief I2S pin configuration structure
 */
typedef struct I2S_pin_config {
    int bck;   ///< GPIO number for I2S BCK (bit clock)
    int ws;    ///< GPIO number for I2S WS (word select / LRCK)
    int dout;  ///< GPIO number for I2S data output
    int din;   ///< GPIO number for I2S data input
} I2S_pin_config;

/**
 * @brief I2S TX mode enumeration
 */
typedef enum {
    I2S_TX_MODE_NONE = 0,  ///< I2S TX idle (no active audio)
    I2S_TX_MODE_A2DP,      ///< I2S TX streaming A2DP audio (music)
    I2S_TX_MODE_HFP,       ///< I2S TX streaming HFP audio (voice call)
} i2s_tx_mode_t;

// ============================================================================
// INITIALIZATION & CONFIGURATION
// ============================================================================

/**
 * @brief Initialize I2S driver and create synchronization primitives
 * 
 * Must be called once during system initialization before any other I2S functions.
 * Creates TX and RX channels, semaphores, and mutexes for audio streaming.
 */
void bt_i2s_init(void);

/**
 * @brief Configure TX I2S GPIO pins
 * 
 * @param bckPin  GPIO number for I2S BCK (bit clock)
 * @param wsPin   GPIO number for I2S WS (word select / LRCK)
 * @param doPin   GPIO number for I2S DOUT (data output to codec/amplifier)
 * @param diPin   GPIO number for I2S DIN (unused for TX, set to 0)
 */
void bt_i2s_set_tx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin);

/**
 * @brief Configure RX I2S GPIO pins
 * 
 * @param bckPin  GPIO number for I2S BCK (bit clock)
 * @param wsPin   GPIO number for I2S WS (word select / LRCK)
 * @param doPin   GPIO number for I2S DOUT (unused for RX, set to 0)
 * @param diPin   GPIO number for I2S DIN (data input from microphone)
 */
void bt_i2s_set_rx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin);

/**
 * @brief Uninstall I2S driver and free all resources
 * 
 * Disables both TX and RX channels and deletes channel handles.
 * Should be called during system shutdown.
 */
void bt_i2s_driver_uninstall(void);

// ============================================================================
// A2DP MODE CONTROL (Music Streaming)
// ============================================================================

/**
 * @brief Start A2DP audio streaming mode
 * 
 * Configures I2S for A2DP (44.1kHz stereo), creates decode task and TX task,
 * and starts audio playback. Waits for HFP mode to stop if currently active.
 */
void bt_i2s_a2dp_start(void);

/**
 * @brief Stop A2DP audio streaming mode
 * 
 * Signals tasks to stop, waits for clean shutdown, deletes ringbuffers,
 * and disables I2S TX channel. Enters idle mode.
 */
void bt_i2s_a2dp_stop(void);

/**
 * @brief Set A2DP SBC packet parameters (called by A2DP callback)
 * 
 * @param packet_size        Size of SBC packet in bytes (typically 952)
 * @param frames_per_packet  Number of SBC frames per packet (typically 8)
 */
void bt_i2s_a2dp_set_packet_params(uint16_t packet_size, uint8_t frames_per_packet);

/**
 * @brief Write raw SBC encoded data to A2DP decode ringbuffer
 * 
 * Called from Bluetooth stack callback. Data is queued for SBC decode task.
 * 
 * @param data  Pointer to SBC encoded audio data
 * @param len   Length of SBC data in bytes
 */
void bt_i2s_a2dp_write_sbc_encoded_ringbuf(const uint8_t *data, uint32_t len);

/**
 * @brief Set A2DP audio configuration (sample rate and channel count)
 * 
 * Call this from the A2DP audio configuration callback when stream parameters are received.
 * This must be called BEFORE bt_i2s_a2dp_start() to properly configure the I2S hardware.
 * 
 * @param sample_rate  Sample rate in Hz (44100, 48000, 32000, or 16000)
 * @param ch_count     Channel mode (I2S_SLOT_MODE_MONO or I2S_SLOT_MODE_STEREO)
 */
void bt_i2s_a2dp_set_audio_config(int sample_rate, int ch_count);

// ============================================================================
// HFP MODE CONTROL (Voice Call)
// ============================================================================

/**
 * @brief Start HFP audio streaming mode
 * 
 * Configures I2S for HFP (16kHz mono), opens mSBC codec, creates TX/RX tasks,
 * and starts bidirectional audio streaming. Waits for A2DP mode to stop if active.
 */
void bt_i2s_hfp_start(void);

/**
 * @brief Stop HFP audio streaming mode
 * 
 * Unregisters HFP audio callback, closes mSBC codec, signals tasks to stop,
 * waits for clean shutdown, deletes ringbuffers, and disables I2S channels.
 */
void bt_i2s_hfp_stop(void);

/**
 * @brief Write decoded HFP audio data to TX ringbuffer (speaker output)
 * 
 * Called from HFP audio data callback. Data is queued for I2S TX task.
 * 
 * @param data  Pointer to decoded PCM audio data
 * @param size  Size of PCM data in bytes
 */
void bt_i2s_hfp_write_tx_ringbuf(const uint8_t *data, uint32_t size);

/**
 * @brief Read encoded HFP audio data from RX ringbuffer (microphone input)
 * 
 * Called by HFP client to retrieve encoded audio data for transmission.
 * 
 * @param mic_data  Buffer to store encoded audio data (mSBC frame)
 * @return Number of bytes read (60 bytes for mSBC frame, or 0 if none available)
 */
size_t bt_i2s_hfp_read_rx_ringbuf(uint8_t *mic_data);

// ============================================================================
// MODE QUERY FUNCTIONS
// ============================================================================

/**
 * @brief Get current I2S TX mode
 * 
 * @return Current mode: I2S_TX_MODE_NONE, I2S_TX_MODE_A2DP, or I2S_TX_MODE_HFP
 */
i2s_tx_mode_t bt_i2s_get_tx_mode(void);

/**
 * @brief Check if HFP mode is currently active
 * 
 * @return true if in HFP mode, false otherwise
 */
bool bt_i2s_is_hfp_mode(void);

/**
 * @brief Check if A2DP mode is currently active
 * 
 * @return true if A2DP mode, false otherwise
 */
bool bt_i2s_is_a2dp_mode(void);

/**
 * @brief Get TX I2S channel handle
 * 
 * @return TX channel handle (for advanced use - direct I2S writes)
 */
i2s_chan_handle_t bt_i2s_get_tx_chan(void);

/**
 * @brief Get RX I2S channel handle
 * 
 * @return RX channel handle (for advanced use - direct I2S reads)
 */
i2s_chan_handle_t bt_i2s_get_rx_chan(void);

// ============================================================================
// Volume control
// ============================================================================

/**
 * @brief Set A2DP output volume (0-15)
 * Scales PCM samples before I2S output
 */
void bt_i2s_set_a2dp_volume(uint8_t volume);

/**
 * @brief Set HFP speaker volume (0-15)
 * Scales PCM samples before I2S output
 */
void bt_i2s_set_hfp_speaker_volume(uint8_t volume);

/**
 * @brief Set HFP microphone volume (0-15)
 * Scales PCM samples before encoding
 */
void bt_i2s_set_hfp_mic_volume(uint8_t volume);

/**
 * @brief Get current A2DP volume
 */
uint8_t bt_i2s_get_a2dp_volume(void);

/**
 * @brief Get current HFP speaker volume
 */
uint8_t bt_i2s_get_hfp_speaker_volume(void);

/**
 * @brief Get current HFP microphone volume
 */
uint8_t bt_i2s_get_hfp_mic_volume(void);

#ifdef __cplusplus
}
#endif

#endif /* __BT_I2S_H__ */
