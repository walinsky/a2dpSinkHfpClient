#ifndef __A2DPSINK_HFPHF_H__
#define __A2DPSINK_HFPHF_H__

#include "esp_err.h"
#include "esp_bt_defs.h"
#include <stdint.h>
#include <stdbool.h>
#include "bt_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - hide implementation details
typedef struct a2dpSinkHfpHf_config_t a2dpSinkHfpHf_config_t;
typedef struct a2dpSinkHfpHf_contact_t a2dpSinkHfpHf_contact_t;
typedef struct a2dpSinkHfpHf_phone_number_t a2dpSinkHfpHf_phone_number_t;
typedef void* a2dpSinkHfpHf_phonebook_handle_t;

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

// ============================================================================
// DEVICE CONTROL API
// ============================================================================

esp_err_t a2dpSinkHfpHf_start_discovery(void);
esp_err_t a2dpSinkHfpHf_cancel_discovery(void);
const char* a2dpSinkHfpHf_get_device_name(void);
bool a2dpSinkHfpHf_is_connected(void);
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code);
esp_err_t a2dpSinkHfpHf_set_pin(const char *pin_code, uint8_t pin_len);

// ============================================================================
// AVRC API
// ============================================================================

bool a2dpSinkHfpHf_avrc_play(void);
bool a2dpSinkHfpHf_avrc_pause(void);
bool a2dpSinkHfpHf_avrc_next(void);
bool a2dpSinkHfpHf_avrc_prev(void);

#ifdef __cplusplus
}
#endif

#endif // __A2DPSINK_HFPHF_H__
