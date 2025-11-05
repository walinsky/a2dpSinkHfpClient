/*
 * ringtone.c - Simple 2-second ringtone beep
 */

#include "ringtone.h"
#include "bt_i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define TAG "RINGTONE"
#define RINGTONE_SAMPLE_RATE 16000
#define RINGTONE_BUFFER_SIZE 1600  // 100ms of audio
#define RINGTONE_DURATION_MS 2000   // 2 seconds

extern i2s_chan_handle_t tx_chan;

static TaskHandle_t ringtone_task_handle = NULL;
static volatile bool ringtone_stop_requested = false;

// Generate dual-tone ringtone
static void generate_ringtone_buffer(int16_t *buffer, size_t samples, uint32_t phase)
{
    const float freq1 = 440.0f;  // Hz - A note
    const float freq2 = 880.0f;  // Hz - A note, one octave up
    const float sample_rate = RINGTONE_SAMPLE_RATE;
    
    for (size_t i = 0; i < samples; i++) {
        float t = (phase + i) / sample_rate;
        
        // Mix two frequencies
        float sample1 = sinf(2.0f * M_PI * freq1 * t);
        float sample2 = sinf(2.0f * M_PI * freq2 * t);
        float mixed = (sample1 + sample2 * 0.5f) / 1.5f;
        
        // Apply amplitude (30% volume)
        buffer[i] = (int16_t)(mixed * 32767.0f * 0.3f);
    }
}

static void ringtone_beep_task(void *arg)
{
    int16_t *buffer = (int16_t *)malloc(RINGTONE_BUFFER_SIZE * sizeof(int16_t));
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate ringtone buffer");
        ringtone_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    uint32_t phase = 0;
    size_t bytes_written;
    uint32_t elapsed_ms = 0;
    
    ESP_LOGI(TAG, "Playing ringtone beep");
    
    // Play for 2 seconds or until stop requested
    while (elapsed_ms < RINGTONE_DURATION_MS && !ringtone_stop_requested) {
        // Generate tone
        generate_ringtone_buffer(buffer, RINGTONE_BUFFER_SIZE, phase);
        phase += RINGTONE_BUFFER_SIZE;
        
        // Write to I2S
        if (tx_chan != NULL) {
            esp_err_t ret = i2s_channel_write(tx_chan, buffer, 
                                              RINGTONE_BUFFER_SIZE * sizeof(int16_t), 
                                              &bytes_written, pdMS_TO_TICKS(100));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write to I2S: %d", ret);
                break;
            }
        }
        
        elapsed_ms += 100;  // 100ms per buffer
        vTaskDelay(1);
    }
    
    // Send brief silence to clean up
    memset(buffer, 0, RINGTONE_BUFFER_SIZE * sizeof(int16_t));
    if (tx_chan != NULL) {
        i2s_channel_write(tx_chan, buffer, RINGTONE_BUFFER_SIZE * sizeof(int16_t), 
                         &bytes_written, pdMS_TO_TICKS(100));
    }
    
    free(buffer);
    ESP_LOGD(TAG, "Ringtone beep finished");
    
    ringtone_stop_requested = false;
    ringtone_task_handle = NULL;
    vTaskDelete(NULL);
}

void ringtone_play_beep(void)
{
    // If already playing, let it finish naturally
    if (ringtone_task_handle != NULL) {
        ESP_LOGD(TAG, "Ringtone already playing, skipping");
        return;
    }
    
    ringtone_stop_requested = false;
    
    BaseType_t ret = xTaskCreate(ringtone_beep_task, "ringtone_beep", 
                                  3072, NULL, 5, &ringtone_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ringtone task");
    }
}

void ringtone_stop(void)
{
    if (ringtone_task_handle != NULL) {
        ESP_LOGI(TAG, "Stopping ringtone");
        ringtone_stop_requested = true;
        
        // Wait briefly for task to stop
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
