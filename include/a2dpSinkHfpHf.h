#ifndef __A2DPSINK_HFPHF_H__
#define __A2DPSINK_HFPHF_H__

#include "esp_err.h"
#include "esp_bt_defs.h"
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
 * @brief Bluetooth connection state callback.
 *
 * @param connected True if connected, false if disconnected.
 * @param remote_bda Remote device Bluetooth address (or NULL on disconnect).
 */
typedef void (*bt_connection_cb_t)(bool connected, const uint8_t *remote_bda);

/**
 * @brief A2DP audio stream state callback.
 *
 * @param streaming True if audio streaming started, false if stopped.
 */
typedef void (*a2dp_audio_state_cb_t)(bool streaming);

/**
 * @brief HFP call state callback.
 *
 * @param call_active True if call is active/ringing, false if no call.
 * @param call_state HFP call state (idle, incoming, outgoing, active).
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
    char type[16]; // "CELL", "HOME", "WORK", etc.
};

struct a2dpSinkHfpHf_contact_t {
    char full_name[MAX_NAME_LEN];
    a2dpSinkHfpHf_phone_number_t phones[MAX_PHONES_PER_CONTACT];
    uint8_t phone_count;
};

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize the A2DP Sink + HFP Hands-Free component.
 *
 * @param config Optional configuration structure; if NULL, Kconfig defaults are used.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_init(const a2dpSinkHfpHf_config_t *config);

/**
 * @brief Deinitialize the A2DP Sink + HFP Hands-Free component.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_deinit(void);

/**
 * @brief Configure the A2DP Sink + HFP Hands-Free component before init.
 *
 * @param config Pointer to configuration structure to be stored for init.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_config(const a2dpSinkHfpHf_config_t *config);

// ============================================================================
// GAP API
// ============================================================================

/**
 * @brief Register a callback for GAP events.
 *
 * @param callback GAP event callback to register.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_register_gap_callback(bt_gap_event_cb_t callback);

/**
 * @brief Unregister a GAP event callback.
 *
 * @param callback GAP event callback to unregister.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_unregister_gap_callback(bt_gap_event_cb_t callback);

// ============================================================================
// PHONEBOOK API
// ============================================================================

/**
 * @brief Get the current phonebook handle.
 *
 * @return Phonebook handle or NULL if not available.
 */
a2dpSinkHfpHf_phonebook_handle_t a2dpSinkHfpHf_get_phonebook(void);

/**
 * @brief Get total number of contacts in phonebook.
 *
 * @param pb Phonebook handle.
 * @return Number of contacts; 0 if handle is invalid.
 */
uint16_t a2dpSinkHfpHf_phonebook_get_count(a2dpSinkHfpHf_phonebook_handle_t pb);

/**
 * @brief Search phonebook for contacts starting with a specific letter.
 *
 * @param pb Phonebook handle.
 * @param letter Letter to search (A-Z).
 * @param count Output pointer for number of contacts found.
 * @return Array of contacts or NULL if none found; internal storage, do not free.
 */
a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_letter(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    char letter,
    uint16_t *count
);

/**
 * @brief Search phonebook by name substring.
 *
 * @param pb Phonebook handle.
 * @param name Name substring to search.
 * @param count Output pointer for number of contacts found.
 * @return Array of contacts or NULL if none found; internal storage, do not free.
 */
a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_name(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *name,
    uint16_t *count
);

/**
 * @brief Search phonebook by phone number.
 *
 * @param pb Phonebook handle.
 * @param number Phone number to search.
 * @return Contact pointer or NULL if not found; internal storage, do not free.
 */
a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_number(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *number
);

/**
 * @brief Get phone numbers for a specific contact.
 *
 * @param pb Phonebook handle.
 * @param full_name Full name of contact.
 * @param count Output pointer for number of phone numbers.
 * @return Array of phone numbers or NULL if not found; internal storage, do not free.
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
 * @brief Notify when HFP audio connection state changes.
 *
 * @param connected True if HFP audio is connected, false otherwise.
 */
void bt_hfp_audio_connection_state_changed(bool connected);

/**
 * @brief Notify when HFP call state changes.
 *
 * @param call_active True if call is active, false otherwise.
 * @param call_state ESP-IDF call state value.
 */
void hfp_notify_call_state(bool call_active, int call_state);

/**
 * @brief Answer an incoming call.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_answer_call(void);

/**
 * @brief Reject an incoming call.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_reject_call(void);

/**
 * @brief Hang up the active call.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_hangup_call(void);

/**
 * @brief Dial a phone number.
 *
 * @param number Null-terminated phone number string to dial.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_dial_number(const char *number);

/**
 * @brief Redial the last dialed number.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_redial(void);

/**
 * @brief Dial from a memory location.
 *
 * @param location Memory location index (typically 1-99).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_dial_memory(int location);

/**
 * @brief Start voice recognition (Siri, Google Assistant, etc.).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_start_voice_recognition(void);

/**
 * @brief Stop voice recognition.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_stop_voice_recognition(void);

/**
 * @brief Query current network operator name.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_query_operator(void);

/**
 * @brief Query current calls list.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_query_current_calls(void);

/**
 * @brief Retrieve subscriber information.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_retrieve_subscriber_info(void);

/**
 * @brief Send Bluetooth Response and Hold (BTRH) command.
 *
 * @param btrh BTRH command value (0–2).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_send_btrh(int btrh);

/**
 * @brief Send XAPL vendor and feature information to remote device.
 *
 * @param features String in the form "Product-Version,Features" (e.g. "iPhone-6.3.0,2").
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_send_xapl(const char *features);

/**
 * @brief Send iPhone battery level and dock state (IPHONACCEV).
 *
 * @param bat_level Battery level 0–9, or -1 to skip.
 * @param docked Dock state: 0/1, or -1 to skip.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_send_iphoneaccev(int bat_level, int docked);

// ============================================================================
// DEVICE CONTROL API
// ============================================================================

/**
 * @brief Start Bluetooth device discovery.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_start_discovery(void);

/**
 * @brief Cancel Bluetooth device discovery.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_cancel_discovery(void);

/**
 * @brief Get the configured Bluetooth device name.
 *
 * @return Pointer to null-terminated device name string.
 */
const char* a2dpSinkHfpHf_get_device_name(void);

/**
 * @brief Check whether any A2DP connection is active.
 *
 * @return True if connected, false otherwise.
 */
bool a2dpSinkHfpHf_is_connected(void);

/**
 * @brief Set the country code for phonebook number formatting.
 *
 * @param country_code Two or three digit country code string (e.g. "31").
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code);

/**
 * @brief Set Bluetooth pairing PIN code.
 *
 * @param pin_code Pointer to PIN code buffer.
 * @param pin_len Length of PIN code in bytes.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_set_pin(const char *pin_code, uint8_t pin_len);

/**
 * @brief Get current Bluetooth pairing PIN code.
 *
 * @param pin_code Output buffer for PIN code.
 * @param pin_len Input: size of buffer; Output: actual PIN length.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_get_pin(char *pin_code, uint8_t *pin_len);

/**
 * @brief Register callback for Bluetooth connection events.
 *
 * @param callback Connection callback function (NULL to unregister).
 */
void a2dp_sink_hfp_hf_register_connection_cb(bt_connection_cb_t callback);

/**
 * @brief Register callback for A2DP audio streaming state.
 *
 * @param callback Audio state callback function (NULL to unregister).
 */
void a2dp_sink_hfp_hf_register_audio_state_cb(a2dp_audio_state_cb_t callback);

/**
 * @brief Register callback for HFP call state changes.
 *
 * @param callback Call state callback function (NULL to unregister).
 */
void a2dp_sink_hfp_hf_register_call_state_cb(hfp_call_state_cb_t callback);

/**
 * @brief Notify registered connection callback of A2DP/HFP connection change.
 *
 * @param connected True if connected, false if disconnected.
 * @param bda Remote device Bluetooth address; may be NULL on disconnect.
 */
void a2dp_sink_notify_connection(bool connected, const uint8_t *bda);

/**
 * @brief Notify registered audio state callback of A2DP audio state change.
 *
 * @param streaming True if audio streaming, false if stopped.
 */
void a2dp_sink_notify_audio_state(bool streaming);

// ============================================================================
// AVRC API
// ============================================================================

/**
 * @brief Set AVRCP metadata attribute mask.
 *
 * @param attr_mask Bitmask of AVRCP metadata attributes to request.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_set_avrc_metadata_mask(uint8_t attr_mask);

/**
 * @brief Register AVRCP connection state callback.
 *
 * @param callback AVRCP connection state callback.
 */
void a2dpSinkHfpHf_register_avrc_conn_callback(bt_avrc_conn_state_cb_t callback);

/**
 * @brief Register AVRCP metadata callback.
 *
 * @param callback AVRCP metadata callback.
 */
void a2dpSinkHfpHf_register_avrc_metadata_callback(bt_avrc_metadata_cb_t callback);

/**
 * @brief Register AVRCP playback status callback.
 *
 * @param callback AVRCP playback status callback.
 */
void a2dpSinkHfpHf_register_avrc_playback_callback(bt_avrc_playback_status_cb_t callback);

/**
 * @brief Register AVRCP volume change callback.
 *
 * @param callback AVRCP volume change callback.
 */
void a2dpSinkHfpHf_register_avrc_volume_callback(bt_avrc_volume_cb_t callback);

/**
 * @brief Get latest AVRCP metadata.
 *
 * @return Pointer to last received AVRCP metadata structure.
 */
const bt_avrc_metadata_t* a2dpSinkHfpHf_get_avrc_metadata(void);

/**
 * @brief Check whether AVRCP control channel is connected.
 *
 * @return True if AVRCP is connected, false otherwise.
 */
bool a2dpSinkHfpHf_is_avrc_connected(void);

/**
 * @brief Send AVRCP Play command.
 *
 * @return True on success, false otherwise.
 */
bool a2dpSinkHfpHf_avrc_play(void);

/**
 * @brief Send AVRCP Pause command.
 *
 * @return True on success, false otherwise.
 */
bool a2dpSinkHfpHf_avrc_pause(void);

/**
 * @brief Send AVRCP Next Track command.
 *
 * @return True on success, false otherwise.
 */
bool a2dpSinkHfpHf_avrc_next(void);

/**
 * @brief Send AVRCP Previous Track command.
 *
 * @return True on success, false otherwise.
 */
bool a2dpSinkHfpHf_avrc_prev(void);

// ============================================================================
// Volume control
// ============================================================================

/**
 * @brief Set HFP Hands-Free speaker volume.
 *
 * @param volume Volume level (0–15, per HFP specification).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_set_hfp_speaker_volume(uint8_t volume);

/**
 * @brief Set HFP Hands-Free microphone volume.
 *
 * @param volume Volume level (0–15, per HFP specification).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_set_hfp_mic_volume(uint8_t volume);

/**
 * @brief Set A2DP sink volume and sync via AVRCP absolute volume.
 *
 * @param volume Local volume level (0–15) mapped to AVRCP 0–127 for phone.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t a2dpSinkHfpHf_set_a2dp_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif // __A2DPSINK_HFPHF_H__
