/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_pbac_api.h"
#include "bt_app_pbac.h"
#include "phonebook.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define BT_PBAC_TAG "BT_PBAC"
#define PBAC_QUEUE_SIZE 50
#define PBAC_TASK_STACK_SIZE 8192
#define PBAC_TASK_PRIORITY 1
#define PHONEBOOK_PAGE_SIZE 15

esp_pbac_conn_hdl_t pba_conn_handle;

static phonebook_t *current_phonebook = NULL;
static esp_bd_addr_t current_device_addr = {0};

static QueueHandle_t pbac_data_queue = NULL;
static TaskHandle_t pbac_task_handle = NULL;

// Pagination state
static uint16_t total_phonebook_size = 0;
static uint16_t current_offset = 0;
static bool pagination_in_progress = false;
/* static bool s_pbac_paused = false; */

typedef enum {
    PBAC_MSG_DATA_CHUNK,
    PBAC_MSG_FINALIZE,
} pbac_msg_type_t;

typedef struct {
    pbac_msg_type_t type;
    uint16_t data_len;
    char *data;  // Pointer to malloc'd data
} pbac_msg_t;



/* void bt_app_pbac_pause(void)
{
    ESP_LOGI(BT_PBAC_TAG, "Pausing phonebook sync");
    s_pbac_paused = true;
}

void bt_app_pbac_resume(void)
{
    ESP_LOGI(BT_PBAC_TAG, "Resuming phonebook sync");
    s_pbac_paused = false;
} */

static void pbac_processing_task(void *arg)
{
    pbac_msg_t msg;
    
    ESP_LOGI(BT_PBAC_TAG, "Phonebook processing task started");
    
    while (1) {
        if (xQueueReceive(pbac_data_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            // Skip processing if paused
/*             if (s_pbac_paused) {
                ESP_LOGD(BT_PBAC_TAG, "Phonebook sync paused, skipping message");
                if (msg.data) {
                    free(msg.data);
                }
                continue;
            } */

            if (msg.type == PBAC_MSG_DATA_CHUNK) {
                if (current_phonebook != NULL && msg.data != NULL) {
                    esp_err_t err = phonebook_process_chunk(current_phonebook, msg.data, msg.data_len);
                    if (err != ESP_OK) {
                        ESP_LOGE(BT_PBAC_TAG, "Failed to process phonebook chunk: 0x%x", err);
                    }
                    // Free malloc'd data
                    free(msg.data);
                }
            } 
            else if (msg.type == PBAC_MSG_FINALIZE) {
                if (current_phonebook != NULL) {
                    phonebook_finalize_sync(current_phonebook);
                    ESP_LOGI(BT_PBAC_TAG, "Phonebook sync complete: %d contacts stored", 
                            phonebook_get_count(current_phonebook));
                    
                    uint16_t count;
                    contact_t *results;
                    
                    results = phonebook_search_by_letter(current_phonebook, 'A', &count);
                    ESP_LOGI(BT_PBAC_TAG, "Contacts starting with 'A': %d", count);
                    if (results) free(results);
                    
                    results = phonebook_search_by_letter(current_phonebook, 'D', &count);
                    ESP_LOGI(BT_PBAC_TAG, "Contacts starting with 'D': %d", count);
                    if (results) free(results);
                }
            }
            
            // Only yield if queue is empty
            if (uxQueueMessagesWaiting(pbac_data_queue) == 0) {
                vTaskDelay(1);
            }
        }
    }
}

void bt_app_pbac_task_start(void)
{
    pbac_data_queue = xQueueCreate(PBAC_QUEUE_SIZE, sizeof(pbac_msg_t));
    if (pbac_data_queue == NULL) {
        ESP_LOGE(BT_PBAC_TAG, "Failed to create pbac data queue");
        return;
    }
    
    BaseType_t ret = xTaskCreate(pbac_processing_task, 
                                  "pbac_proc", 
                                  PBAC_TASK_STACK_SIZE, 
                                  NULL, 
                                  PBAC_TASK_PRIORITY, 
                                  &pbac_task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(BT_PBAC_TAG, "Failed to create pbac processing task");
        vQueueDelete(pbac_data_queue);
        pbac_data_queue = NULL;
    } else {
        ESP_LOGI(BT_PBAC_TAG, "Phonebook processing task created successfully");
    }
}

void bt_app_pbac_cb(esp_pbac_event_t event, esp_pbac_param_t *param)
{
    switch (event)
    {
    case ESP_PBAC_CONNECTION_STATE_EVT:
        ESP_LOGI(BT_PBAC_TAG, "PBA client connection event, state: %s, reason: 0x%x", 
                (param->conn_stat.connected ? "Connected" : "Disconnected"), 
                param->conn_stat.reason);
        
        if (param->conn_stat.connected) {
            ESP_LOGI(BT_PBAC_TAG, "Remote device: %02x:%02x:%02x:%02x:%02x:%02x",
                    param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                    param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                    param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
            ESP_LOGI(BT_PBAC_TAG, "Peer supported repositories: 0x%x, supported features: 0x%lx", 
                    param->conn_stat.peer_supported_repo, 
                    param->conn_stat.peer_supported_feat);
            
            pba_conn_handle = param->conn_stat.handle;
            memcpy(current_device_addr, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            
            current_phonebook = phonebook_get_or_create(current_device_addr);
            if (current_phonebook == NULL) {
                ESP_LOGE(BT_PBAC_TAG, "Failed to create phonebook");
            } else {
                ESP_LOGI(BT_PBAC_TAG, "Phonebook initialized for device");
            }
            
            // Reset pagination state
            total_phonebook_size = 0;
            current_offset = 0;
            pagination_in_progress = false;
            
            esp_pbac_set_phone_book(pba_conn_handle, ESP_PBAC_SET_PHONE_BOOK_FLAGS_DOWN, "telecom");
        } else {
            ESP_LOGI(BT_PBAC_TAG, "Disconnected from device");
            current_phonebook = NULL;
            memset(current_device_addr, 0, ESP_BD_ADDR_LEN);
            pagination_in_progress = false;
        }
        break;
        
    case ESP_PBAC_PULL_PHONE_BOOK_RESPONSE_EVT:
        if (param->pull_phone_book_rsp.result == ESP_PBAC_SUCCESS && 
            param->pull_phone_book_rsp.data_len > 0) {
            
            // Malloc for each chunk (freed by processing task)
            char *data_copy = (char*)malloc(param->pull_phone_book_rsp.data_len + 1);
            if (data_copy != NULL) {
                memcpy(data_copy, param->pull_phone_book_rsp.data, param->pull_phone_book_rsp.data_len);
                data_copy[param->pull_phone_book_rsp.data_len] = '\0';
                
                pbac_msg_t msg;
                msg.type = PBAC_MSG_DATA_CHUNK;
                msg.data_len = param->pull_phone_book_rsp.data_len;
                msg.data = data_copy;
                
                if (xQueueSend(pbac_data_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
                    ESP_LOGW(BT_PBAC_TAG, "Queue full, dropping chunk");
                    free(data_copy);
                }
            } else {
                ESP_LOGE(BT_PBAC_TAG, "Failed to allocate memory for chunk (%d bytes)", 
                        param->pull_phone_book_rsp.data_len);
            }
        }
        
        if (param->pull_phone_book_rsp.final) {
            ESP_LOGI(BT_PBAC_TAG, "PBA client pull phone book final response");
            
            // Check if this is the initial size query
            if (param->pull_phone_book_rsp.include_phone_book_size && !pagination_in_progress) {
                total_phonebook_size = param->pull_phone_book_rsp.phone_book_size;
                current_offset = 0;
                pagination_in_progress = true;
                
                ESP_LOGI(BT_PBAC_TAG, "Phone Book Size: %d, starting paginated download", 
                        total_phonebook_size);
                
                // Start downloading first page
                esp_pbac_pull_phone_book_app_param_t app_param = {0};
                app_param.include_property_selector = 1;
                app_param.property_selector = 0xFFFFFFF7;  // Filter out photo
                app_param.include_max_list_count = 1;
                app_param.max_list_count = PHONEBOOK_PAGE_SIZE;
                app_param.include_list_start_offset = 1;
                app_param.list_start_offset = current_offset;
                
                esp_pbac_pull_phone_book(pba_conn_handle, "telecom/pb.vcf", &app_param);
            }
            // Check if we need to download more pages
            else if (pagination_in_progress) {
                // Wait for queue to drain before requesting next page
                int wait_count = 0;
                while (uxQueueMessagesWaiting(pbac_data_queue) > 0 && wait_count < 100) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    wait_count++;
                }
                
                ESP_LOGI(BT_PBAC_TAG, "Queue drained (waited %d ms)", wait_count * 50);
                
                // Move to next page
                current_offset += PHONEBOOK_PAGE_SIZE;
                
                if (current_offset < total_phonebook_size) {
                    ESP_LOGI(BT_PBAC_TAG, "Downloading contacts %d-%d of %d", 
                            current_offset, 
                            current_offset + PHONEBOOK_PAGE_SIZE - 1,
                            total_phonebook_size);
                    
                    // Request next page
                    esp_pbac_pull_phone_book_app_param_t app_param = {0};
                    app_param.include_property_selector = 1;
                    app_param.property_selector = 0xFFFFFFF7;
                    app_param.include_max_list_count = 1;
                    app_param.max_list_count = PHONEBOOK_PAGE_SIZE;
                    app_param.include_list_start_offset = 1;
                    app_param.list_start_offset = current_offset;
                    
                    esp_pbac_pull_phone_book(pba_conn_handle, "telecom/pb.vcf", &app_param);
                } else {
                    // All pages downloaded - finalize
                    ESP_LOGI(BT_PBAC_TAG, "All pages downloaded, finalizing");
                    pagination_in_progress = false;
                    
                    pbac_msg_t msg;
                    msg.type = PBAC_MSG_FINALIZE;
                    msg.data_len = 0;
                    msg.data = NULL;
                    xQueueSend(pbac_data_queue, &msg, portMAX_DELAY);
                }
            }
        }
        break;
        
    case ESP_PBAC_SET_PHONE_BOOK_RESPONSE_EVT:
        ESP_LOGI(BT_PBAC_TAG, "PBA client set phone book response, handle:%d, result: 0x%x", 
                param->set_phone_book_rsp.handle, 
                param->set_phone_book_rsp.result);
        if (param->set_phone_book_rsp.result == ESP_PBAC_SUCCESS) {
            // First, query the phonebook size
            esp_pbac_pull_phone_book_app_param_t app_param = {0};
            app_param.include_max_list_count = 1;
            app_param.max_list_count = 0;  // 0 = query size only
            esp_pbac_pull_phone_book(pba_conn_handle, "telecom/pb.vcf", &app_param);
        }
        break;
        
    case ESP_PBAC_PULL_VCARD_LISTING_RESPONSE_EVT:
        if (param->pull_vcard_listing_rsp.final) {
            ESP_LOGI(BT_PBAC_TAG, "PBA client pull vCard listing final response");
        }
        break;
        
    case ESP_PBAC_PULL_VCARD_ENTRY_RESPONSE_EVT:
        if (param->pull_vcard_entry_rsp.final) {
            ESP_LOGI(BT_PBAC_TAG, "PBA client pull vCard entry final response");
        }
        break;
        
    default:
        break;
    }
}

phonebook_t* bt_app_pbac_get_current_phonebook(void)
{
    return current_phonebook;
}

void bt_app_pbac_search_contacts(const char *query)
{
    if (current_phonebook == NULL) {
        ESP_LOGW(BT_PBAC_TAG, "No phonebook available");
        return;
    }
    
    uint16_t count;
    contact_t *results = phonebook_search_by_name(current_phonebook, query, &count);
    
    ESP_LOGI(BT_PBAC_TAG, "Search for '%s' found %d contacts:", query, count);
    for (int i = 0; i < count; i++) {
        phonebook_print_contact(&results[i]);
    }
    
    if (results) free(results);
}

void bt_app_pbac_list_contacts_by_letter(char letter)
{
    if (current_phonebook == NULL) {
        ESP_LOGW(BT_PBAC_TAG, "No phonebook available");
        return;
    }
    
    uint16_t count;
    contact_t *results = phonebook_search_by_letter(current_phonebook, letter, &count);
    
    ESP_LOGI(BT_PBAC_TAG, "Contacts starting with '%c': %d", letter, count);
    for (int i = 0; i < count; i++) {
        phonebook_print_contact(&results[i]);
    }
    
    if (results) free(results);
}

contact_t* bt_app_pbac_find_by_number(const char *number)
{
    if (current_phonebook == NULL) {
        ESP_LOGW(BT_PBAC_TAG, "No phonebook available");
        return NULL;
    }
    
    contact_t *contact = phonebook_search_by_number(current_phonebook, number);
    
    if (contact) {
        ESP_LOGI(BT_PBAC_TAG, "Found contact for number %s:", number);
        phonebook_print_contact(contact);
    } else {
        ESP_LOGI(BT_PBAC_TAG, "No contact found for number %s", number);
    }
    
    return contact;
}
