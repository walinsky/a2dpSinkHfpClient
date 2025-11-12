/*
 * ESP32 A2DP Sink + HFP Hands-Free Example
 * 
 * Follows the pattern from app_hf_msg_set.c
 * Uses esp_console framework properly
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_console.h"
// #include "esp_vfs_dev.h"
// #include "esp_vfs_fat.h"
#include "driver/uart.h"
#include "a2dpSinkHfpHf.h"

#define TAG "HFP_EXAMPLE"

// ============================================================================
// CONFIG
// ============================================================================
// just do your config in menuconfig!!!

// or
// if you insist

// here:
// #define BT_DEVICE_NAME "ESP32-HFP-Demo"
// #define COUNTRY_CODE "1"

// // I2S PINS
// #define I2S_TX_BCK  26
// #define I2S_TX_WS   17
// #define I2S_TX_DOUT 25
// #define I2S_RX_BCK  16
// #define I2S_RX_WS   27
// #define I2S_RX_DIN  14

// and in app main:
// // Configure Bluetooth
// a2dpSinkHfpHf_config_t config = {
//     .device_name = BT_DEVICE_NAME,
//     .i2s_tx_bck = I2S_TX_BCK,
//     .i2s_tx_ws = I2S_TX_WS,
//     .i2s_tx_dout = I2S_TX_DOUT,
//     .i2s_rx_bck = I2S_RX_BCK,
//     .i2s_rx_ws = I2S_RX_WS,
//     .i2s_rx_din = I2S_RX_DIN
// };
// and call:
// ESP_ERROR_CHECK(a2dpSinkHfpHf_init(&config));
// instead of:
// ESP_ERROR_CHECK(a2dpSinkHfpHf_init(NULL));


// ============================================================================
// AVRC CALLBACKS
// ============================================================================

void metadata_callback(const bt_avrc_metadata_t *metadata) {
    if (metadata && metadata->valid) {
        ESP_LOGI(TAG, "Now Playing: %s - %s", metadata->artist, metadata->title);
    }
}

void playback_callback(const bt_avrc_playback_status_t *status) {
    const char *state_str;
    switch (status->status) {
        case ESP_AVRC_PLAYBACK_STOPPED: state_str = "Stopped"; break;
        case ESP_AVRC_PLAYBACK_PLAYING: state_str = "Playing"; break;
        case ESP_AVRC_PLAYBACK_PAUSED:  state_str = "Paused"; break;
        default: return;
    }
    ESP_LOGI(TAG, "Playback: %s", state_str);
}

// ============================================================================
// COMMAND HANDLERS (following app_hf_msg_set.c pattern)
// ============================================================================

#define HFP_CMD_HANDLER(cmd) static int hfp_##cmd##_handler(int argn, char **argv)

// Answer call
HFP_CMD_HANDLER(answer) {
    printf("Answer call\n");
    a2dpSinkHfpHf_answer_call();
    return 0;
}

// Reject call
HFP_CMD_HANDLER(reject) {
    printf("Reject call\n");
    a2dpSinkHfpHf_reject_call();
    return 0;
}

// Hang up
HFP_CMD_HANDLER(hangup) {
    printf("Hang up\n");
    a2dpSinkHfpHf_hangup_call();
    return 0;
}

// Dial number
HFP_CMD_HANDLER(dial) {
    if (argn != 2) {
        printf("Insufficient arguments\n");
        return 1;
    }
    printf("Dial: %s\n", argv[1]);
    a2dpSinkHfpHf_dial_number(argv[1]);
    return 0;
}

// Redial
HFP_CMD_HANDLER(redial) {
    printf("Redial\n");
    a2dpSinkHfpHf_redial();
    return 0;
}

// Dial memory
HFP_CMD_HANDLER(dial_mem) {
    if (argn != 2) {
        printf("Insufficient arguments\n");
        return 1;
    }
    int location;
    if (sscanf(argv[1], "%d", &location) != 1) {
        printf("Invalid argument: %s\n", argv[1]);
        return 1;
    }
    printf("Dial memory: %d\n", location);
    a2dpSinkHfpHf_dial_memory(location);
    return 0;
}

// Start voice recognition
HFP_CMD_HANDLER(vr_start) {
    printf("Start voice recognition\n");
    a2dpSinkHfpHf_start_voice_recognition();
    return 0;
}

// Stop voice recognition
HFP_CMD_HANDLER(vr_stop) {
    printf("Stop voice recognition\n");
    a2dpSinkHfpHf_stop_voice_recognition();
    return 0;
}

// Volume update
HFP_CMD_HANDLER(volume) {
    if (argn != 3) {
        printf("Usage: vol <spk|mic> <0-15>\n");
        return 1;
    }

    const char *target = argv[1];
    int volume;
    if (sscanf(argv[2], "%d", &volume) != 1 || volume < 0 || volume > 15) {
        printf("Invalid volume: %s\n", argv[2]);
        return 1;
    }

    printf("Volume %s = %d\n", target, volume);
    a2dpSinkHfpHf_volume_update(target, volume);
    return 0;
}

// Query operator
HFP_CMD_HANDLER(query_op) {
    printf("Query operator\n");
    a2dpSinkHfpHf_query_operator();
    return 0;
}

// Query calls
HFP_CMD_HANDLER(query_calls) {
    printf("Query calls\n");
    a2dpSinkHfpHf_query_current_calls();
    return 0;
}

// Retrieve subscriber
HFP_CMD_HANDLER(subscriber) {
    printf("Retrieve subscriber info\n");
    a2dpSinkHfpHf_retrieve_subscriber_info();
    return 0;
}

// AVRC Play
HFP_CMD_HANDLER(play) {
    if (a2dpSinkHfpHf_avrc_play()) {
        printf("Play\n");
    } else {
        printf("AVRC not connected\n");
    }
    return 0;
}

// AVRC Pause
HFP_CMD_HANDLER(pause) {
    if (a2dpSinkHfpHf_avrc_pause()) {
        printf("Pause\n");
    } else {
        printf("AVRC not connected\n");
    }
    return 0;
}

// AVRC Next
HFP_CMD_HANDLER(next) {
    if (a2dpSinkHfpHf_avrc_next()) {
        printf("Next\n");
    } else {
        printf("AVRC not connected\n");
    }
    return 0;
}

// AVRC Previous
HFP_CMD_HANDLER(prev) {
    if (a2dpSinkHfpHf_avrc_prev()) {
        printf("Previous\n");
    } else {
        printf("AVRC not connected\n");
    }
    return 0;
}

// Status
HFP_CMD_HANDLER(status) {
    printf("\n");
    printf("Bluetooth: %s\n", a2dpSinkHfpHf_is_connected() ? "Connected" : "Disconnected");
    printf("AVRC:      %s\n", a2dpSinkHfpHf_is_avrc_connected() ? "Connected" : "Disconnected");

    const bt_avrc_metadata_t *metadata = a2dpSinkHfpHf_get_avrc_metadata();
    if (metadata && metadata->valid) {
        printf("Playing: %s - %s\n", metadata->artist, metadata->title);
    }
    printf("\n");
    return 0;
}

// ============================================================================
// COMMAND REGISTRATION
// ============================================================================

void register_hfp_commands(void) {
    const esp_console_cmd_t answer_cmd = {
        .command = "ac",
        .help = "Answer call",
        .hint = NULL,
        .func = &hfp_answer_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&answer_cmd));

    const esp_console_cmd_t reject_cmd = {
        .command = "rc",
        .help = "Reject call",
        .hint = NULL,
        .func = &hfp_reject_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reject_cmd));

    const esp_console_cmd_t hangup_cmd = {
        .command = "hc",
        .help = "Hang up call",
        .hint = NULL,
        .func = &hfp_hangup_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&hangup_cmd));

    const esp_console_cmd_t dial_cmd = {
        .command = "d",
        .help = "Dial number, e.g. d 1234567890",
        .hint = "<number>",
        .func = &hfp_dial_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&dial_cmd));

    const esp_console_cmd_t redial_cmd = {
        .command = "rd",
        .help = "Redial",
        .hint = NULL,
        .func = &hfp_redial_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&redial_cmd));

    const esp_console_cmd_t dial_mem_cmd = {
        .command = "dm",
        .help = "Dial memory, e.g. dm 5",
        .hint = "<location>",
        .func = &hfp_dial_mem_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&dial_mem_cmd));

    const esp_console_cmd_t vr_start_cmd = {
        .command = "vron",
        .help = "Start voice recognition",
        .hint = NULL,
        .func = &hfp_vr_start_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&vr_start_cmd));

    const esp_console_cmd_t vr_stop_cmd = {
        .command = "vroff",
        .help = "Stop voice recognition",
        .hint = NULL,
        .func = &hfp_vr_stop_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&vr_stop_cmd));

    const esp_console_cmd_t volume_cmd = {
        .command = "vol",
        .help = "Volume control, e.g. vol spk 10",
        .hint = "<spk|mic> <0-15>",
        .func = &hfp_volume_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&volume_cmd));

    const esp_console_cmd_t query_op_cmd = {
        .command = "qop",
        .help = "Query network operator",
        .hint = NULL,
        .func = &hfp_query_op_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&query_op_cmd));

    const esp_console_cmd_t query_calls_cmd = {
        .command = "qc",
        .help = "Query current calls",
        .hint = NULL,
        .func = &hfp_query_calls_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&query_calls_cmd));

    const esp_console_cmd_t subscriber_cmd = {
        .command = "rs",
        .help = "Retrieve subscriber info",
        .hint = NULL,
        .func = &hfp_subscriber_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&subscriber_cmd));

    const esp_console_cmd_t play_cmd = {
        .command = "play",
        .help = "Play (AVRC)",
        .hint = NULL,
        .func = &hfp_play_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&play_cmd));

    const esp_console_cmd_t pause_cmd = {
        .command = "pause",
        .help = "Pause (AVRC)",
        .hint = NULL,
        .func = &hfp_pause_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&pause_cmd));

    const esp_console_cmd_t next_cmd = {
        .command = "next",
        .help = "Next track (AVRC)",
        .hint = NULL,
        .func = &hfp_next_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&next_cmd));

    const esp_console_cmd_t prev_cmd = {
        .command = "prev",
        .help = "Previous track (AVRC)",
        .hint = NULL,
        .func = &hfp_prev_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&prev_cmd));

    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show status",
        .hint = NULL,
        .func = &hfp_status_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));
}

// ============================================================================
// CONSOLE INITIALIZATION
// ============================================================================

void initialize_console(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "hfp>";
    repl_config.max_cmdline_length = 256;

    // Register commands
    register_hfp_commands();

    esp_console_register_help_command();

    // Start REPL
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

// ============================================================================
// MAIN
// ============================================================================

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP32 HFP Hands-Free Demo");

    // Register callbacks
    a2dpSinkHfpHf_register_avrc_metadata_callback(metadata_callback);
    a2dpSinkHfpHf_register_avrc_playback_callback(playback_callback);

    // Initialize Bluetooth
    ESP_ERROR_CHECK(a2dpSinkHfpHf_init(NULL));

    ESP_LOGI(TAG, "Bluetooth initialized. Type 'help' for commands.");

    // Initialize console (this blocks in REPL mode)
    initialize_console();
}
