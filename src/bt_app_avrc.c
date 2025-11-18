/**
 * @file bt_app_avrc.c
 * @brief AVRCP Controller and Target - Queue-based (ESP-IDF 5.5.1)
 * 
 * This implementation moves ALL event processing out of BTC_TASK context
 * immediately to avoid stack overflow. Events are posted to a queue and
 * processed in a dedicated task with proper stack space.
 */

#include "bt_app_avrc.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *BT_AVRC_TAG = "BT_AVRC";
static uint8_t s_metadata_attr_mask = 0;

#define AVRC_EVENT_QUEUE_SIZE 10
#define AVRC_TASK_STACK_SIZE  (3 * 1024)
#define AVRC_TASK_PRIORITY    5

/* ============================================
 * Event Queue (Moves out of BTC context ASAP)
 * ============================================ */

typedef enum {
    AVRC_EVT_METADATA,
    AVRC_EVT_PLAYBACK_STATUS,
    AVRC_EVT_VOLUME_CHANGE,
    AVRC_EVT_CONNECTION_STATE
} avrc_event_type_t;

typedef struct {
    avrc_event_type_t type;
    union {
        struct {
            esp_avrc_md_attr_mask_t attr_id;
            char text[256];
            int length;
        } metadata;
        struct {
            uint8_t play_status;
            uint32_t song_length;
            uint32_t song_position;
        } playback;
        uint8_t volume;
        bool connected;
    } data;
} avrc_event_t;

/* ============================================
 * State Management
 * ============================================ */

typedef struct {
    bt_avrc_conn_state_t conn_state;
    bt_avrc_metadata_t metadata;
    bt_avrc_playback_status_t playback_status;
    uint8_t volume;
    uint8_t tl;  // Transaction label counter
    
    // Event queue and task
    QueueHandle_t event_queue;
    TaskHandle_t event_task;
    
    // Application callbacks
    bt_avrc_conn_state_cb_t conn_cb;
    bt_avrc_metadata_cb_t metadata_cb;
    bt_avrc_playback_status_cb_t playback_cb;
    bt_avrc_volume_cb_t volume_cb;
} bt_avrc_state_t;

static bt_avrc_state_t s_avrc_state = {
    .conn_state = BT_AVRC_STATE_DISCONNECTED,
    .volume = 0xFF,
    .tl = 0,
    .event_queue = NULL,
    .event_task = NULL,
    .conn_cb = NULL,
    .metadata_cb = NULL,
    .playback_cb = NULL,
    .volume_cb = NULL
};

/* ============================================
 * Helper Functions
 * ============================================ */

static inline uint8_t bt_avrc_next_tl(void)
{
    s_avrc_state.tl = (s_avrc_state.tl + 1) & ESP_AVRC_TRANS_LABEL_MAX;
    return s_avrc_state.tl;
}

/* ============================================
 * Event Processing Task (Safe context)
 * ============================================ */

static void bt_avrc_event_task(void *arg)
{
    avrc_event_t evt;
    
    ESP_LOGI(BT_AVRC_TAG, "Event processing task started");
    
    while (1) {
        if (xQueueReceive(s_avrc_state.event_queue, &evt, portMAX_DELAY)) {
            switch (evt.type) {
            case AVRC_EVT_METADATA:
                s_avrc_state.metadata.valid = true;
                
                switch (evt.data.metadata.attr_id) {
                case ESP_AVRC_MD_ATTR_TITLE:
                    strncpy(s_avrc_state.metadata.title, evt.data.metadata.text, BT_AVRC_META_TEXT_MAX_LEN - 1);
                    s_avrc_state.metadata.title[BT_AVRC_META_TEXT_MAX_LEN - 1] = '\0';
                    ESP_LOGI(BT_AVRC_TAG, "📀 Track: %s", s_avrc_state.metadata.title);
                    break;
                case ESP_AVRC_MD_ATTR_ARTIST:
                    strncpy(s_avrc_state.metadata.artist, evt.data.metadata.text, BT_AVRC_META_TEXT_MAX_LEN - 1);
                    s_avrc_state.metadata.artist[BT_AVRC_META_TEXT_MAX_LEN - 1] = '\0';
                    ESP_LOGI(BT_AVRC_TAG, "🎤 Artist: %s", s_avrc_state.metadata.artist);
                    break;
                case ESP_AVRC_MD_ATTR_ALBUM:
                    strncpy(s_avrc_state.metadata.album, evt.data.metadata.text, BT_AVRC_META_TEXT_MAX_LEN - 1);
                    s_avrc_state.metadata.album[BT_AVRC_META_TEXT_MAX_LEN - 1] = '\0';
                    ESP_LOGI(BT_AVRC_TAG, "💿 Album: %s", s_avrc_state.metadata.album);
                    break;
                case ESP_AVRC_MD_ATTR_GENRE:
                    strncpy(s_avrc_state.metadata.genre, evt.data.metadata.text, BT_AVRC_META_TEXT_MAX_LEN - 1);
                    s_avrc_state.metadata.genre[BT_AVRC_META_TEXT_MAX_LEN - 1] = '\0';
                    break;
                case ESP_AVRC_MD_ATTR_TRACK_NUM:
                    s_avrc_state.metadata.track_num = atoi(evt.data.metadata.text);
                    break;
                case ESP_AVRC_MD_ATTR_NUM_TRACKS:
                    s_avrc_state.metadata.total_tracks = atoi(evt.data.metadata.text);
                    break;
                case ESP_AVRC_MD_ATTR_PLAYING_TIME:
                    s_avrc_state.metadata.playing_time_ms = atoi(evt.data.metadata.text);
                    break;
                default:
                    break;
                }
                
                if (s_avrc_state.metadata_cb) {
                    s_avrc_state.metadata_cb(&s_avrc_state.metadata);
                }
                break;
                
            case AVRC_EVT_PLAYBACK_STATUS:
                s_avrc_state.playback_status.status = evt.data.playback.play_status;
                s_avrc_state.playback_status.song_len_ms = evt.data.playback.song_length;
                s_avrc_state.playback_status.song_pos_ms = evt.data.playback.song_position;
                
                if (s_avrc_state.playback_cb) {
                    s_avrc_state.playback_cb(&s_avrc_state.playback_status);
                }
                break;
                
            case AVRC_EVT_VOLUME_CHANGE:
                s_avrc_state.volume = evt.data.volume;
                
                if (s_avrc_state.volume_cb) {
                    s_avrc_state.volume_cb(evt.data.volume);
                }
                break;
                
            case AVRC_EVT_CONNECTION_STATE:
                s_avrc_state.conn_state = evt.data.connected ? BT_AVRC_STATE_CONNECTED : BT_AVRC_STATE_DISCONNECTED;
                
                if (!evt.data.connected) {
                    memset(&s_avrc_state.metadata, 0, sizeof(bt_avrc_metadata_t));
                    memset(&s_avrc_state.playback_status, 0, sizeof(bt_avrc_playback_status_t));
                    s_avrc_state.volume = 0xFF;
                }
                
                if (s_avrc_state.conn_cb) {
                    s_avrc_state.conn_cb(evt.data.connected);
                }
                break;
            }
        }
    }
}

/* ============================================
 * AVRCP Controller Callback (BTC_TASK context - keep minimal!)
 * ============================================ */

static void bt_avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    avrc_event_t evt;
    
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        ESP_LOGI(BT_AVRC_TAG, "AVRC CT connection: %s", 
                 param->conn_stat.connected ? "connected" : "disconnected");
        
        // Post to queue immediately
        evt.type = AVRC_EVT_CONNECTION_STATE;
        evt.data.connected = param->conn_stat.connected;
        xQueueSend(s_avrc_state.event_queue, &evt, 0);
        
        if (param->conn_stat.connected) {
            esp_avrc_ct_send_get_rn_capabilities_cmd(bt_avrc_next_tl());
        }
        break;
    }
    
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        // Post to queue immediately - NO processing in BTC_TASK!
        evt.type = AVRC_EVT_METADATA;
        evt.data.metadata.attr_id = param->meta_rsp.attr_id;
        evt.data.metadata.length = param->meta_rsp.attr_length;
        strncpy(evt.data.metadata.text, (const char *)param->meta_rsp.attr_text, sizeof(evt.data.metadata.text) - 1);
        evt.data.metadata.text[sizeof(evt.data.metadata.text) - 1] = '\0';
        xQueueSend(s_avrc_state.event_queue, &evt, 0);
        break;
    }
    
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT: {
        // Post to queue immediately
        evt.type = AVRC_EVT_PLAYBACK_STATUS;
        evt.data.playback.play_status = param->play_status_rsp.play_status;
        evt.data.playback.song_length = param->play_status_rsp.song_length;
        evt.data.playback.song_position = param->play_status_rsp.song_position;
        xQueueSend(s_avrc_state.event_queue, &evt, 0);
        break;
    }
    
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        switch (param->change_ntf.event_id) {
        case ESP_AVRC_RN_TRACK_CHANGE:
            ESP_LOGI(BT_AVRC_TAG, "Track changed");
            esp_avrc_ct_send_metadata_cmd(bt_avrc_next_tl(), s_metadata_attr_mask);
            esp_avrc_ct_send_register_notification_cmd(bt_avrc_next_tl(), ESP_AVRC_RN_TRACK_CHANGE, 0);
            break;
            
        case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
            evt.type = AVRC_EVT_PLAYBACK_STATUS;
            evt.data.playback.play_status = param->change_ntf.event_parameter.playback;
            evt.data.playback.song_length = 0;
            evt.data.playback.song_position = 0;
            xQueueSend(s_avrc_state.event_queue, &evt, 0);
            esp_avrc_ct_send_register_notification_cmd(bt_avrc_next_tl(), ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
            break;
            
        case ESP_AVRC_RN_VOLUME_CHANGE:
            evt.type = AVRC_EVT_VOLUME_CHANGE;
            evt.data.volume = param->change_ntf.event_parameter.volume;
            xQueueSend(s_avrc_state.event_queue, &evt, 0);
            esp_avrc_ct_send_register_notification_cmd(bt_avrc_next_tl(), ESP_AVRC_RN_VOLUME_CHANGE, 0);
            break;
        }
        break;
    }
    
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        ESP_LOGI(BT_AVRC_TAG, "Got capabilities, registering for notifications");
        esp_avrc_ct_send_register_notification_cmd(bt_avrc_next_tl(), ESP_AVRC_RN_TRACK_CHANGE, 0);
        esp_avrc_ct_send_register_notification_cmd(bt_avrc_next_tl(), ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
        esp_avrc_ct_send_register_notification_cmd(bt_avrc_next_tl(), ESP_AVRC_RN_VOLUME_CHANGE, 0);
        esp_avrc_ct_send_metadata_cmd(bt_avrc_next_tl(), s_metadata_attr_mask);
        break;
    }
    
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        evt.type = AVRC_EVT_VOLUME_CHANGE;
        evt.data.volume = param->set_volume_rsp.volume;
        xQueueSend(s_avrc_state.event_queue, &evt, 0);
        break;
    }
    
    default:
        break;
    }
}

/* ============================================
 * AVRCP Target Callback
 * ============================================ */

static void bt_avrc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    avrc_event_t evt;
    
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        ESP_LOGI(BT_AVRC_TAG, "AVRC TG connection: %s",
                 param->conn_stat.connected ? "connected" : "disconnected");
        break;
        
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
        evt.type = AVRC_EVT_VOLUME_CHANGE;
        evt.data.volume = param->set_abs_vol.volume;
        xQueueSend(s_avrc_state.event_queue, &evt, 0);
        break;
        
    default:
        break;
    }
}

/**
 * @brief Build AVRC metadata attribute mask from Kconfig settings
 * @return uint8_t attribute mask for esp_avrc_ct_send_metadata_cmd
 */
static uint8_t bt_app_avrc_get_metadata_mask(void)
{
    uint8_t mask = 0;

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_TITLE
        mask |= ESP_AVRC_MD_ATTR_TITLE;
    #endif

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_ARTIST
        mask |= ESP_AVRC_MD_ATTR_ARTIST;
    #endif

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_ALBUM
        mask |= ESP_AVRC_MD_ATTR_ALBUM;
    #endif

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_TRACK_NUM
        mask |= ESP_AVRC_MD_ATTR_TRACK_NUM;
    #endif

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_NUM_TRACKS
        mask |= ESP_AVRC_MD_ATTR_NUM_TRACKS;
    #endif

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_GENRE
        mask |= ESP_AVRC_MD_ATTR_GENRE;
    #endif

    #ifdef CONFIG_A2DPSINK_HFPHF_AVRC_METADATA_PLAYING_TIME
        mask |= ESP_AVRC_MD_ATTR_PLAYING_TIME;
    #endif

    // Ensure at least one attribute is requested
    if (mask == 0) {
        ESP_LOGW(BT_AVRC_TAG, "No metadata attributes configured, defaulting to TITLE");
        mask = ESP_AVRC_MD_ATTR_TITLE;
    }

    return mask;
}

/* ============================================
 * Public API
 * ============================================ */

 bool bt_app_avrc_set_metadata_mask(uint8_t attr_mask)
{
    if (s_avrc_state.event_queue != NULL) {
        ESP_LOGE(BT_AVRC_TAG, "Cannot change metadata mask after initialization");
        return false;
    }

    if (attr_mask == 0) {
        ESP_LOGE(BT_AVRC_TAG, "Invalid metadata mask (cannot be 0)");
        return false;
    }

    s_metadata_attr_mask = attr_mask;
    ESP_LOGI(BT_AVRC_TAG, "Custom metadata mask set: 0x%02X", attr_mask);
    return true;
}

bool bt_app_avrc_init(void)
{
    ESP_LOGI(BT_AVRC_TAG, "Initializing AVRCP with queue-based architecture");
    
    // Build metadata attribute mask from Kconfig (if not already set via API)
    if (s_metadata_attr_mask == 0) {
        s_metadata_attr_mask = bt_app_avrc_get_metadata_mask();
        ESP_LOGI(BT_AVRC_TAG, "Using Kconfig metadata mask: 0x%02X", s_metadata_attr_mask);
    } else {
        ESP_LOGI(BT_AVRC_TAG, "Using custom metadata mask: 0x%02X", s_metadata_attr_mask);
    }

    // Create event queue
    s_avrc_state.event_queue = xQueueCreate(AVRC_EVENT_QUEUE_SIZE, sizeof(avrc_event_t));
    if (!s_avrc_state.event_queue) {
        ESP_LOGE(BT_AVRC_TAG, "Failed to create event queue");
        return false;
    }
    
    // Create event processing task
    BaseType_t ret = xTaskCreate(bt_avrc_event_task, "avrc_evt", AVRC_TASK_STACK_SIZE, 
                                  NULL, AVRC_TASK_PRIORITY, &s_avrc_state.event_task);
    if (ret != pdPASS) {
        ESP_LOGE(BT_AVRC_TAG, "Failed to create event task");
        vQueueDelete(s_avrc_state.event_queue);
        return false;
    }
    
    // Initialize AVRCP Controller
    esp_err_t err = esp_avrc_ct_init();
    if (err != ESP_OK) {
        ESP_LOGE(BT_AVRC_TAG, "Failed to init AVRC CT: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    err = esp_avrc_ct_register_callback(bt_avrc_ct_callback);
    if (err != ESP_OK) {
        ESP_LOGE(BT_AVRC_TAG, "Failed to register AVRC CT callback");
        esp_avrc_ct_deinit();
        goto cleanup;
    }
    
    // Initialize AVRCP Target
    err = esp_avrc_tg_init();
    if (err != ESP_OK) {
        ESP_LOGE(BT_AVRC_TAG, "Failed to init AVRC TG");
        esp_avrc_ct_deinit();
        goto cleanup;
    }
    
    err = esp_avrc_tg_register_callback(bt_avrc_tg_callback);
    if (err != ESP_OK) {
        ESP_LOGE(BT_AVRC_TAG, "Failed to register AVRC TG callback");
        esp_avrc_tg_deinit();
        esp_avrc_ct_deinit();
        goto cleanup;
    }
    
    ESP_LOGI(BT_AVRC_TAG, "AVRCP initialized successfully");
    return true;
    
cleanup:
    if (s_avrc_state.event_task) {
        vTaskDelete(s_avrc_state.event_task);
        s_avrc_state.event_task = NULL;
    }
    if (s_avrc_state.event_queue) {
        vQueueDelete(s_avrc_state.event_queue);
        s_avrc_state.event_queue = NULL;
    }
    return false;
}

void bt_app_avrc_deinit(void)
{
    ESP_LOGI(BT_AVRC_TAG, "Deinitializing AVRCP");
    
    esp_avrc_tg_deinit();
    esp_avrc_ct_deinit();
    
    if (s_avrc_state.event_task) {
        vTaskDelete(s_avrc_state.event_task);
        s_avrc_state.event_task = NULL;
    }
    
    if (s_avrc_state.event_queue) {
        vQueueDelete(s_avrc_state.event_queue);
        s_avrc_state.event_queue = NULL;
    }
    
    memset(&s_avrc_state, 0, sizeof(bt_avrc_state_t));
    s_avrc_state.volume = 0xFF;
}

// Passthrough commands
bool bt_app_avrc_cmd_play(void) 
{
    if (s_avrc_state.conn_state != BT_AVRC_STATE_CONNECTED) return false;
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PRESSED);
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_RELEASED);
    return true;
}

bool bt_app_avrc_cmd_pause(void)
{
    if (s_avrc_state.conn_state != BT_AVRC_STATE_CONNECTED) return false;
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);
    return true;
}

bool bt_app_avrc_cmd_next(void)
{
    if (s_avrc_state.conn_state != BT_AVRC_STATE_CONNECTED) return false;
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
    return true;
}

bool bt_app_avrc_cmd_prev(void)
{
    if (s_avrc_state.conn_state != BT_AVRC_STATE_CONNECTED) return false;
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
    esp_avrc_ct_send_passthrough_cmd(bt_avrc_next_tl(), ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
    return true;
}

/**
 * @brief Set absolute volume via AVRCP
 */
esp_err_t bt_app_avrc_set_absolute_volume(uint8_t volume)
{
    // Check if AVRCP is connected
    if (s_avrc_state.conn_state != BT_AVRC_STATE_CONNECTED) {
        ESP_LOGW(BT_AVRC_TAG, "Cannot set volume: AVRCP not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Validate volume range (AVRCP spec: 0x00-0x7F)
    if (volume > 127) {
        ESP_LOGE(BT_AVRC_TAG, "Invalid volume %d (max 127)", volume);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get next transaction label and send command
    uint8_t tl = bt_avrc_next_tl();
    esp_err_t ret = esp_avrc_ct_send_set_absolute_volume_cmd(tl, volume);
    
    if (ret == ESP_OK) {
        ESP_LOGI(BT_AVRC_TAG, "Set absolute volume to %d (%.1f%%)", 
                 volume, (volume * 100.0f) / 127.0f);
    } else {
        ESP_LOGE(BT_AVRC_TAG, "Failed to set volume: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// Callback registration
void bt_app_avrc_register_conn_callback(bt_avrc_conn_state_cb_t callback)
{
    s_avrc_state.conn_cb = callback;
}

void bt_app_avrc_register_metadata_callback(bt_avrc_metadata_cb_t callback)
{
    s_avrc_state.metadata_cb = callback;
}

void bt_app_avrc_register_playback_status_callback(bt_avrc_playback_status_cb_t callback)
{
    s_avrc_state.playback_cb = callback;
}

void bt_app_avrc_register_volume_callback(bt_avrc_volume_cb_t callback)
{
    s_avrc_state.volume_cb = callback;
}

// Getters
bool bt_app_avrc_is_connected(void)
{
    return s_avrc_state.conn_state == BT_AVRC_STATE_CONNECTED;
}

const bt_avrc_metadata_t* bt_app_avrc_get_metadata(void)
{
    return s_avrc_state.metadata.valid ? &s_avrc_state.metadata : NULL;
}

uint8_t bt_app_avrc_get_volume(void)
{
    return s_avrc_state.volume;
}
