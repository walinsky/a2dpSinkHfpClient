#ifndef __A2DPSINK_HFPHF_H__
#define __A2DPSINK_HFPHF_H__

#include "esp_err.h"
#include "esp_bt_defs.h"
#include <stdint.h>
#include <stdbool.h>
#include "bt_gap.h"
#include "bt_app_avrc.h"
#include "bt_volume_control.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - hide implementation details
typedef struct a2dpSinkHfpHf_config_t a2dpSinkHfpHf_config_t;
typedef struct a2dpSinkHfpHf_contact_t a2dpSinkHfpHf_contact_t;
typedef struct a2dpSinkHfpHf_phone_number_t a2dpSinkHfpHf_phone_number_t;
typedef void* a2dpSinkHfpHf_phonebook_handle_t;

/**
 * @brief Bluetooth connection state callback
 * @param connected true if connected, false if disconnected
 * @param remote_bda Remote device Bluetooth address (or NULL on disconnect)
 */
typedef void (*bt_connection_cb_t)(bool connected, const uint8_t *remote_bda);

/**
 * @brief A2DP audio stream state callback
 * @param streaming true if audio streaming started, false if stopped
 */
typedef void (*a2dp_audio_state_cb_t)(bool streaming);

/**
 * @brief HFP call state callback
 * @param call_active true if call is active/ringing, false if no call
 * @param call_state HFP call state (idle, incoming, outgoing, active)
 */
typedef void (*hfp_call_state_cb_t)(bool call_active, int call_state);

// Configuration structure
struct a2dpSinkHfpHf_config_t {
    const char *device_name;
    int i2s_tx_bck;
    int i2s_tx_ws;
    int i2s_tx_dout;
    int i2s_rx_bck;
    int i2s_rx_ws;
    int i2s_rx_din;
};

// Contact structure
#define MAX_NAME_LEN 64
#define MAX_PHONE_LEN 32
#define MAX_PHONES_PER_CONTACT 5

struct a2dpSinkHfpHf_phone_number_t {
    char number[MAX_PHONE_LEN];
    char type[16];  // "CELL", "HOME", "WORK", etc.
};

struct a2dpSinkHfpHf_contact_t {
    char full_name[MAX_NAME_LEN];
    a2dpSinkHfpHf_phone_number_t phones[MAX_PHONES_PER_CONTACT];
    uint8_t phone_count;
};

// ============================================================================
// INITIALIZATION
// ============================================================================

esp_err_t a2dpSinkHfpHf_init(const a2dpSinkHfpHf_config_t *config);
esp_err_t a2dpSinkHfpHf_deinit(void);
esp_err_t a2dpSinkHfpHf_config(const a2dpSinkHfpHf_config_t *config);

// ============================================================================
// GAP API
// ============================================================================

/**
 * @brief Register a callback for GAP events
 * This allows applications to subscribe to Bluetooth connection/disconnection events
 * @param callback Callback function
 * @return ESP_OK on success
 */
esp_err_t a2dpSinkHfpHf_register_gap_callback(bt_gap_event_cb_t callback);

/**
 * @brief Unregister a GAP event callback
 * @param callback Callback function to unregister
 * @return ESP_OK on success
 */
esp_err_t a2dpSinkHfpHf_unregister_gap_callback(bt_gap_event_cb_t callback);

// ============================================================================
// PHONEBOOK API
// ============================================================================

/**
 * @brief Get the current phonebook handle
 * @return Phonebook handle or NULL if not available
 */
a2dpSinkHfpHf_phonebook_handle_t a2dpSinkHfpHf_get_phonebook(void);

/**
 * @brief Get total number of contacts in phonebook
 * @param pb Phonebook handle
 * @return Number of contacts
 */
uint16_t a2dpSinkHfpHf_phonebook_get_count(a2dpSinkHfpHf_phonebook_handle_t pb);

/**
 * @brief Search phonebook for contacts starting with a specific letter
 * @param pb Phonebook handle
 * @param letter Letter to search (A-Z)
 * @param count Output: number of contacts found
 * @return Array of contacts (caller must free), or NULL if none found
 */
a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_letter(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    char letter,
    uint16_t *count
);

/**
 * @brief Search phonebook by name substring
 * @param pb Phonebook handle
 * @param name Name substring to search
 * @param count Output: number of contacts found
 * @return Array of contacts (caller must free), or NULL if none found
 */
a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_name(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *name,
    uint16_t *count
);

/**
 * @brief Search phonebook by phone number
 * @param pb Phonebook handle
 * @param number Phone number to search
 * @return Contact (caller must free), or NULL if not found
 */
a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_number(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *number
);

/**
 * @brief Get phone numbers for a specific contact
 * @param pb Phonebook handle
 * @param full_name Full name of contact
 * @param count Output: number of phone numbers
 * @return Array of phone numbers (caller must free), or NULL if not found
 */
a2dpSinkHfpHf_phone_number_t* a2dpSinkHfpHf_phonebook_get_numbers(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *full_name,
    uint8_t *count
);

// ============================================================================
// HFP CALL CONTROL API
// ============================================================================

/**
 * @brief Notify when HFP audio connection state changes
 * 
 * @param connected true if HFP audio is connected, false otherwise
 * 
 * @note This should be called by bt_app_hf.c when HFP audio state changes
 */
void bt_hfp_audio_connection_state_changed(bool connected);

/**
 * @brief Notify when HFP call state changes
 * 
 * @param call_active true if call is active, false otherwise
 * @param call_state ESP-IDF call state value
 * 
 * @note This should be called by bt_app_hf.c when call indicator changes
 *       (ESP_HF_CIND_CALL_EVT)
 */
void hfp_notify_call_state(bool call_active, int call_state);

/**
 * @brief Answer an incoming call
 */
esp_err_t a2dpSinkHfpHf_answer_call(void);

/**
 * @brief Reject an incoming call
 */
esp_err_t a2dpSinkHfpHf_reject_call(void);

/**
 * @brief Hang up active call
 */
esp_err_t a2dpSinkHfpHf_hangup_call(void);

/**
 * @brief Dial a phone number
 * @param number Phone number to dial
 */
esp_err_t a2dpSinkHfpHf_dial_number(const char *number);

/**
 * @brief Redial last dialed number
 */
esp_err_t a2dpSinkHfpHf_redial(void);

/**
 * @brief Dial from memory location
 * @param location Memory location (1-99)
 */
esp_err_t a2dpSinkHfpHf_dial_memory(int location);

/**
 * @brief Start voice recognition (Siri, Google Assistant, etc.)
 */
esp_err_t a2dpSinkHfpHf_start_voice_recognition(void);

/**
 * @brief Stop voice recognition
 */
esp_err_t a2dpSinkHfpHf_stop_voice_recognition(void);

/**
 * @brief Update speaker or microphone volume
 * @param target "spk" for speaker, "mic" for microphone
 * @param volume Volume level (0-15)
 */
esp_err_t a2dpSinkHfpHf_volume_update(const char *target, int volume);

esp_err_t a2dpSinkHfpHf_query_operator(void);
esp_err_t a2dpSinkHfpHf_query_current_calls(void);
esp_err_t a2dpSinkHfpHf_retrieve_subscriber_info(void);

// ============================================================================
// DEVICE CONTROL API
// ============================================================================

esp_err_t a2dpSinkHfpHf_start_discovery(void);
esp_err_t a2dpSinkHfpHf_cancel_discovery(void);
const char* a2dpSinkHfpHf_get_device_name(void);
bool a2dpSinkHfpHf_is_connected(void);
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code);
esp_err_t a2dpSinkHfpHf_set_pin(const char *pin_code, uint8_t pin_len);
/**
 * @brief Register callback for Bluetooth connection events
 * @param callback Callback function (NULL to unregister)
 */
void a2dp_sink_hfp_hf_register_connection_cb(bt_connection_cb_t callback);

/**
 * @brief Register callback for A2DP audio streaming state
 * @param callback Callback function (NULL to unregister)
 */
void a2dp_sink_hfp_hf_register_audio_state_cb(a2dp_audio_state_cb_t callback);

/**
 * @brief Register callback for HFP call state changes
 * @param callback Callback function (NULL to unregister)
 */
void a2dp_sink_hfp_hf_register_call_state_cb(hfp_call_state_cb_t callback);

// ============================================================================
// AVRC API
// ============================================================================

esp_err_t a2dpSinkHfpHf_set_avrc_metadata_mask(uint8_t attr_mask);
void a2dpSinkHfpHf_register_avrc_conn_callback(bt_avrc_conn_state_cb_t callback);
void a2dpSinkHfpHf_register_avrc_metadata_callback(bt_avrc_metadata_cb_t callback);
void a2dpSinkHfpHf_register_avrc_playback_callback(bt_avrc_playback_status_cb_t callback);
void a2dpSinkHfpHf_register_avrc_volume_callback(bt_avrc_volume_cb_t callback);
const bt_avrc_metadata_t* a2dpSinkHfpHf_get_avrc_metadata(void);
bool a2dpSinkHfpHf_is_avrc_connected(void);
bool a2dpSinkHfpHf_avrc_play(void);
bool a2dpSinkHfpHf_avrc_pause(void);
bool a2dpSinkHfpHf_avrc_next(void);
bool a2dpSinkHfpHf_avrc_prev(void);

// ============================================================================
// Volume control
// ============================================================================

/**
 * @brief Set HFP Hands-Free speaker volume
 * 
 * Controls the speaker volume during phone calls (HFP audio output).
 * 
 * @param volume Volume level (0-15, per HFP specification)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t a2dpSinkHfpHf_set_hfp_speaker_volume(uint8_t volume);

/**
 * @brief Set HFP Hands-Free microphone volume
 * 
 * Controls the microphone gain during phone calls (HFP audio input).
 * 
 * @param volume Volume level (0-15, per HFP specification)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t a2dpSinkHfpHf_set_hfp_mic_volume(uint8_t volume);

/**
 * @brief Set A2DP sink volume via AVRCP absolute volume
 * 
 * Sends absolute volume command to connected phone via AVRCP 1.4+.
 * Note: This requires AVRCP connection to be established and the phone
 * to support absolute volume feature. The actual audio scaling happens
 * on the phone side.
 * 
 * @param volume Volume level (0-127, where 0=0% and 127=100%)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t a2dpSinkHfpHf_set_a2dp_volume(uint8_t volume);


#ifdef __cplusplus
}
#endif

#endif // __A2DPSINK_HFPHF_H__
