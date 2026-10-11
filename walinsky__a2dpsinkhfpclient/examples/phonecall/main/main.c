/*
 * ESP32 Phonebook-Based Phone Dialing Example
 * 
 * Use Case: User navigates phonebook alphabetically, selects contact and number,
 * then dials using HFP hands-free profile.
 * 
 * Hardware Requirements:
 * - ESP32 with button connected to GPIO (e.g., GPIO 0 / BOOT button)
 * - Optional: Display for showing contacts
 * 
 * Run menuconfig before compiling!
 * 
 * Button Actions:
 * - Short press: Browse letters A-Z
 * - Long press (2s): Select letter and show contacts
 * - When contacts shown, short press: Next contact
 * - When contact selected, short press: Next phone number
 * - When number selected, long press: Dial
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "a2dpSinkHfpHf.h"

#define TAG "PHONEBOOK_DIAL"

// GPIO Configuration
#define BUTTON_GPIO         GPIO_NUM_23
#define BUTTON_DEBOUNCE_MS  50
#define LONG_PRESS_MS       2000

// UI State Machine
typedef enum {
    STATE_IDLE,
    STATE_BROWSE_LETTERS,
    STATE_SHOW_CONTACTS,
    STATE_SELECT_NUMBER,
    STATE_READY_TO_DIAL
} ui_state_t;

// Global state
static ui_state_t g_ui_state = STATE_IDLE;
static char g_selected_letter = 'A';
static a2dpSinkHfpHf_contact_t *g_contact_list = NULL;
static uint16_t g_contact_count = 0;
static uint16_t g_current_contact_idx = 0;
static a2dpSinkHfpHf_phone_number_t *g_phone_list = NULL;
static uint8_t g_phone_count = 0;
static uint8_t g_current_phone_idx = 0;
static a2dpSinkHfpHf_phonebook_handle_t g_phonebook = NULL;

// Button handling
static volatile uint32_t g_button_press_time = 0;
static volatile bool g_button_pressed = false;

// ============================================================================
// BUTTON INTERRUPT HANDLER
// ============================================================================

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t current_time = xTaskGetTickCountFromISR();
    int button_state = gpio_get_level(BUTTON_GPIO);
    
    if (button_state == 0) {
        g_button_press_time = current_time;
        g_button_pressed = true;
    } else {
        g_button_pressed = false;
    }
}

// ============================================================================
// UI DISPLAY FUNCTIONS
// ============================================================================

static void display_current_state(void)
{
    ESP_LOGI(TAG, "========================================");
    
    switch (g_ui_state) {
        case STATE_IDLE:
            ESP_LOGI(TAG, "Press button to browse phonebook");
            break;
            
        case STATE_BROWSE_LETTERS:
            ESP_LOGI(TAG, "Letter: %c", g_selected_letter);
            ESP_LOGI(TAG, "Short press: Next letter | Long press: Select");
            break;
            
        case STATE_SHOW_CONTACTS:
            if (g_contact_count == 0) {
                ESP_LOGI(TAG, "No contacts found for '%c'", g_selected_letter);
                ESP_LOGI(TAG, "Press button to return");
            } else {
                ESP_LOGI(TAG, "Contact %d/%d:", 
                         g_current_contact_idx + 1, g_contact_count);
                ESP_LOGI(TAG, "Name: %s", 
                         g_contact_list[g_current_contact_idx].full_name);
                ESP_LOGI(TAG, "Phones: %d", 
                         g_contact_list[g_current_contact_idx].phone_count);
                ESP_LOGI(TAG, "Short press: Next | Long press: Select");
            }
            break;
            
        case STATE_SELECT_NUMBER:
            ESP_LOGI(TAG, "Contact: %s", 
                     g_contact_list[g_current_contact_idx].full_name);
            ESP_LOGI(TAG, "Number %d/%d:", g_current_phone_idx + 1, g_phone_count);
            ESP_LOGI(TAG, "  %s (%s)", 
                     g_phone_list[g_current_phone_idx].number,
                     g_phone_list[g_current_phone_idx].type);
            ESP_LOGI(TAG, "Short press: Next | Long press: DIAL");
            break;
            
        case STATE_READY_TO_DIAL:
            ESP_LOGI(TAG, "Ready to dial: %s", 
                     g_phone_list[g_current_phone_idx].number);
            break;
    }
    
    ESP_LOGI(TAG, "========================================");
}

// ============================================================================
// STATE MACHINE HANDLERS
// ============================================================================

static void handle_short_press(void)
{
    switch (g_ui_state) {
        case STATE_IDLE:
            g_ui_state = STATE_BROWSE_LETTERS;
            g_selected_letter = 'A';
            display_current_state();
            break;
            
        case STATE_BROWSE_LETTERS:
            g_selected_letter++;
            if (g_selected_letter > 'Z') {
                g_selected_letter = 'A';
            }
            display_current_state();
            break;
            
        case STATE_SHOW_CONTACTS:
            if (g_contact_count == 0) {
                g_ui_state = STATE_BROWSE_LETTERS;
                display_current_state();
            } else {
                g_current_contact_idx++;
                if (g_current_contact_idx >= g_contact_count) {
                    g_current_contact_idx = 0;
                }
                display_current_state();
            }
            break;
            
        case STATE_SELECT_NUMBER:
            g_current_phone_idx++;
            if (g_current_phone_idx >= g_phone_count) {
                g_current_phone_idx = 0;
            }
            display_current_state();
            break;
            
        case STATE_READY_TO_DIAL:
            g_ui_state = STATE_SELECT_NUMBER;
            display_current_state();
            break;
    }
}

static void handle_long_press(void)
{
    switch (g_ui_state) {
        case STATE_IDLE:
            g_ui_state = STATE_BROWSE_LETTERS;
            g_selected_letter = 'A';
            display_current_state();
            break;
            
        case STATE_BROWSE_LETTERS:
            ESP_LOGI(TAG, "Searching contacts starting with '%c'...", g_selected_letter);
            
            // Get phonebook using public API
            g_phonebook = a2dpSinkHfpHf_get_phonebook();
            if (g_phonebook == NULL) {
                ESP_LOGW(TAG, "Phonebook not ready. Please wait for sync.");
                g_ui_state = STATE_IDLE;
                display_current_state();
                break;
            }
            
            // Free previous results
            if (g_contact_list) {
                free(g_contact_list);
                g_contact_list = NULL;
            }
            
            // Search using public API
            g_contact_list = a2dpSinkHfpHf_phonebook_search_by_letter(
                g_phonebook, g_selected_letter, &g_contact_count);
            g_current_contact_idx = 0;
            g_ui_state = STATE_SHOW_CONTACTS;
            display_current_state();
            break;
            
        case STATE_SHOW_CONTACTS:
            if (g_contact_count == 0) {
                g_ui_state = STATE_BROWSE_LETTERS;
                display_current_state();
            } else {
                a2dpSinkHfpHf_contact_t *selected = &g_contact_list[g_current_contact_idx];
                
                if (g_phone_list) {
                    free(g_phone_list);
                    g_phone_list = NULL;
                }
                
                // Get numbers using public API
                g_phone_list = a2dpSinkHfpHf_phonebook_get_numbers(
                    g_phonebook, selected->full_name, &g_phone_count);
                
                if (g_phone_count == 0) {
                    ESP_LOGW(TAG, "No phone numbers for this contact");
                    break;
                }
                
                g_current_phone_idx = 0;
                g_ui_state = STATE_SELECT_NUMBER;
                display_current_state();
            }
            break;
            
        case STATE_SELECT_NUMBER:
            // DIAL using public API
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "*** DIALING: %s ***", 
                     g_phone_list[g_current_phone_idx].number);
            ESP_LOGI(TAG, "");
            
            a2dpSinkHfpHf_dial_number(g_phone_list[g_current_phone_idx].number);
            
            g_ui_state = STATE_READY_TO_DIAL;
            display_current_state();
            
            vTaskDelay(pdMS_TO_TICKS(3000));
            g_ui_state = STATE_IDLE;
            display_current_state();
            break;
            
        case STATE_READY_TO_DIAL:
            break;
    }
}

// ============================================================================
// BUTTON MONITORING TASK
// ============================================================================

static void button_task(void *arg)
{
    bool last_button_state = false;
    
    while (1) {
        bool current_button_state = g_button_pressed;
        uint32_t current_time = xTaskGetTickCount();
        
        if (last_button_state && !current_button_state) {
            uint32_t press_duration = (current_time - g_button_press_time) * portTICK_PERIOD_MS;
            
            if (press_duration >= BUTTON_DEBOUNCE_MS) {
                if (press_duration >= LONG_PRESS_MS) {
                    ESP_LOGI(TAG, "Long press detected");
                    handle_long_press();
                } else {
                    ESP_LOGI(TAG, "Short press detected");
                    handle_short_press();
                }
            }
        }
        
        last_button_state = current_button_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// BUTTON INITIALIZATION
// ============================================================================

static void init_button(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);
    
    ESP_LOGI(TAG, "Button initialized on GPIO %d", BUTTON_GPIO);
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

void app_main(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32 Phonebook Dialing Example");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize button
    init_button();
    
    // Initialize Bluetooth with Kconfig defaults (NULL = use menuconfig settings)
    ESP_LOGI(TAG, "Initializing Bluetooth...");
    ret = a2dpSinkHfpHf_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Bluetooth initialized successfully");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "INSTRUCTIONS:");
    ESP_LOGI(TAG, "1. Pair your phone with this ESP32");
    ESP_LOGI(TAG, "2. Wait for phonebook sync to complete");
    ESP_LOGI(TAG, "3. Press button to start browsing");
    ESP_LOGI(TAG, "");
    
    display_current_state();
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        if (g_phonebook != NULL) {
            uint16_t total = a2dpSinkHfpHf_phonebook_get_count(g_phonebook);
            if (total > 0) {
                ESP_LOGI(TAG, "Phonebook: %d contacts available", total);
            }
        }
    }
}
