// phonebook.c
#include "phonebook.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"

static const char *TAG = "PHONEBOOK";
static const char *BASE_PATH = "/spiffs";
static phonebook_list_node_t *phonebook_list_head = NULL;
static bool spiffs_mounted = false;
static char g_country_code[4] = DEFAULT_COUNTRY_CODE;

// Helper function to create file path for a device's phonebook
static void make_phonebook_path(esp_bd_addr_t device_addr, char *path_out, size_t path_len)
{
    snprintf(path_out, path_len, "%s/%02x%02x%02x%02x%02x%02x.pb",
             BASE_PATH,
             device_addr[0], device_addr[1], device_addr[2],
             device_addr[3], device_addr[4], device_addr[5]);
}

// Remove all non-digit characters except leading +
static void strip_formatting(const char *input, char *output, size_t output_len)
{
    int out_idx = 0;
    bool first_char = true;
    
    for (int i = 0; input[i] != '\0' && out_idx < output_len - 1; i++) {
        if (isdigit((unsigned char)input[i])) {
            output[out_idx++] = input[i];
            first_char = false;
        } else if (input[i] == '+' && first_char) {
            output[out_idx++] = input[i];
            first_char = false;
        }
    }
    output[out_idx] = '\0';
}

// Normalize phone number to E.164 format (+CountryCodeNumber)
// Optimized: Normalize phone number in-place without extra allocation
static void normalize_phone_number(const char *input, char *output, size_t output_len, const char *country_code)
{
    // Fast path: already normalized
    if (input[0] == '+' && isdigit((unsigned char)input[1])) {
        // Quick check - just copy if already in E.164 format
        bool looks_normalized = true;
        for (int i = 1; input[i] != '\0' && i < output_len - 1; i++) {
            if (!isdigit((unsigned char)input[i])) {
                looks_normalized = false;
                break;
            }
        }
        if (looks_normalized) {
            strncpy(output, input, output_len - 1);
            output[output_len - 1] = '\0';
            return;
        }
    }
    
    // Strip formatting inline
    int out_idx = 0;
    bool has_plus = false;
    
    for (int i = 0; input[i] != '\0' && out_idx < MAX_PHONE_LEN - 1; i++) {
        if (isdigit((unsigned char)input[i])) {
            output[out_idx++] = input[i];
        } else if (input[i] == '+' && i == 0) {
            has_plus = true;
        }
    }
    output[out_idx] = '\0';
    
    // Add country code if needed
    if (has_plus) {
        // Already international - prepend the +
        memmove(output + 1, output, out_idx + 1);
        output[0] = '+';
        return;
    }
    
    // Check for 00 prefix
    if (output[0] == '0' && output[1] == '0') {
        // Convert 00 to +
        memmove(output + 1, output + 2, out_idx - 1);
        output[0] = '+';
        return;
    }
    
    // National number with leading 0
    if (output[0] == '0') {
        // Build: +countrycode + number without leading 0
        char temp[MAX_PHONE_LEN];
        snprintf(temp, MAX_PHONE_LEN, "+%s%s", country_code, output + 1);
        strncpy(output, temp, output_len - 1);
        output[output_len - 1] = '\0';
        return;
    }
    
    // No prefix - add country code
    char temp[MAX_PHONE_LEN];
    snprintf(temp, MAX_PHONE_LEN, "+%s%s", country_code, output);
    strncpy(output, temp, output_len - 1);
    output[output_len - 1] = '\0';
}

// Initialize file with empty contact count at start of sync
static esp_err_t init_phonebook_file(phonebook_t *pb)
{
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    remove(filepath);
    
    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create phonebook file");
        return ESP_FAIL;
    }
    
    uint16_t count = 0;
    fwrite(&count, sizeof(uint16_t), 1, f);
    fclose(f);
    
    return ESP_OK;
}

// Flush batch buffer to file
static esp_err_t flush_write_buffer(phonebook_t *pb)
{
    if (pb->write_buffer_count == 0) {
        return ESP_OK;
    }
    
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "ab");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for batch write");
        return ESP_FAIL;
    }
    
    size_t written = fwrite(pb->write_buffer, sizeof(contact_t), pb->write_buffer_count, f);
    fclose(f);
    
    if (written != pb->write_buffer_count) {
        ESP_LOGE(TAG, "Failed to write all contacts in batch");
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Flushed %d contacts to file", pb->write_buffer_count);
    pb->write_buffer_count = 0;
    
    return ESP_OK;
}

// Add contact to batch buffer (batched writes for performance)
static esp_err_t append_contact_to_file(phonebook_t *pb, const contact_t *contact)
{
    if (pb->write_buffer == NULL) {
        ESP_LOGE(TAG, "Write buffer not allocated");
        return ESP_FAIL;
    }
    
    memcpy(&pb->write_buffer[pb->write_buffer_count], contact, sizeof(contact_t));
    pb->write_buffer_count++;
    
    // Flush if batch is full
    if (pb->write_buffer_count >= CONTACT_BATCH_SIZE) {
        return flush_write_buffer(pb);
    }
    
    return ESP_OK;
}

// Update contact count at end of sync
static esp_err_t update_contact_count_in_file(phonebook_t *pb)
{
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "r+b");
    if (f == NULL) {
        return ESP_FAIL;
    }
    
    fseek(f, 0, SEEK_SET);
    fwrite(&pb->contact_count, sizeof(uint16_t), 1, f);
    fclose(f);
    
    return ESP_OK;
}

// Load contact count from file
static uint16_t load_contact_count(esp_bd_addr_t device_addr)
{
    char filepath[64];
    make_phonebook_path(device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        return 0;
    }
    
    uint16_t count = 0;
    fread(&count, sizeof(uint16_t), 1, f);
    fclose(f);
    
    return count;
}

esp_err_t phonebook_init(void)
{
    phonebook_list_head = NULL;
    
    if (spiffs_mounted) {
        ESP_LOGI(TAG, "SPIFFS already mounted");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition 'storage'");
            ESP_LOGE(TAG, "Make sure partitions.csv is configured and flashed");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS partition size: total: %d, used: %d", total, used);
    }
    
    spiffs_mounted = true;
    ESP_LOGI(TAG, "Phonebook system initialized with SPIFFS storage");
    ESP_LOGI(TAG, "Country code: %s", g_country_code);
    
    return ESP_OK;
}

void phonebook_set_country_code(const char *country_code)
{
    if (country_code != NULL && strlen(country_code) <= 3) {
        strncpy(g_country_code, country_code, sizeof(g_country_code) - 1);
        g_country_code[sizeof(g_country_code) - 1] = '\0';
        ESP_LOGI(TAG, "Country code set to: %s", g_country_code);
    }
}

static bool bd_addr_cmp(esp_bd_addr_t a, esp_bd_addr_t b)
{
    return memcmp(a, b, ESP_BD_ADDR_LEN) == 0;
}

phonebook_t* phonebook_find(esp_bd_addr_t device_addr)
{
    phonebook_list_node_t *node = phonebook_list_head;
    while (node != NULL) {
        if (bd_addr_cmp(node->phonebook.device_addr, device_addr)) {
            return &node->phonebook;
        }
        node = node->next;
    }
    return NULL;
}

phonebook_t* phonebook_get_or_create(esp_bd_addr_t device_addr)
{
    phonebook_t *pb = phonebook_find(device_addr);
    if (pb != NULL) {
        pb->contact_count = 0;
        pb->buffer_pos = 0;
        pb->sync_in_progress = true;
        pb->write_buffer_count = 0;
        memset(pb->vcard_buffer, 0, VCARD_BUFFER_SIZE);
        init_phonebook_file(pb);
        ESP_LOGI(TAG, "Reusing existing phonebook for device");
        return pb;
    }

    phonebook_list_node_t *node = (phonebook_list_node_t*)malloc(sizeof(phonebook_list_node_t));
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to allocate phonebook node");
        return NULL;
    }

    memcpy(node->phonebook.device_addr, device_addr, ESP_BD_ADDR_LEN);
    node->phonebook.contact_count = 0;
    node->phonebook.buffer_pos = 0;
    node->phonebook.sync_in_progress = true;
    node->phonebook.write_buffer_count = 0;
    memset(node->phonebook.vcard_buffer, 0, VCARD_BUFFER_SIZE);

    // Allocate batch write buffer
    node->phonebook.write_buffer = (contact_t*)malloc(sizeof(contact_t) * CONTACT_BATCH_SIZE);
    if (node->phonebook.write_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate write buffer");
        free(node);
        return NULL;
    }

    init_phonebook_file(&node->phonebook);

    node->next = phonebook_list_head;
    phonebook_list_head = node;

    uint16_t stored_count = load_contact_count(device_addr);
    
    ESP_LOGI(TAG, "Created new phonebook for device "
             "%02x:%02x:%02x:%02x:%02x:%02x (previously stored: %d contacts)",
             device_addr[0], device_addr[1], device_addr[2],
             device_addr[3], device_addr[4], device_addr[5],
             stored_count);

    return &node->phonebook;
}

esp_err_t phonebook_delete(esp_bd_addr_t device_addr)
{
    phonebook_list_node_t **node_ptr = &phonebook_list_head;
    
    while (*node_ptr != NULL) {
        if (bd_addr_cmp((*node_ptr)->phonebook.device_addr, device_addr)) {
            phonebook_list_node_t *to_delete = *node_ptr;
            *node_ptr = (*node_ptr)->next;
            
            char filepath[64];
            make_phonebook_path(device_addr, filepath, sizeof(filepath));
            remove(filepath);
            
            if (to_delete->phonebook.write_buffer) {
                free(to_delete->phonebook.write_buffer);
            }
            free(to_delete);
            
            ESP_LOGI(TAG, "Deleted phonebook for device");
            return ESP_OK;
        }
        node_ptr = &(*node_ptr)->next;
    }
    
    return ESP_ERR_NOT_FOUND;
}

static void parse_vcard_line(const char *line, contact_t *contact)
{
    if (strncmp(line, "FN;CHARSET=UTF-8:", 17) == 0) {
        strncpy(contact->full_name, line + 17, MAX_NAME_LEN - 1);
        contact->full_name[MAX_NAME_LEN - 1] = '\0';
    }
    else if (strncmp(line, "FN:", 3) == 0) {
        strncpy(contact->full_name, line + 3, MAX_NAME_LEN - 1);
        contact->full_name[MAX_NAME_LEN - 1] = '\0';
    }
    else if (strncmp(line, "TEL", 3) == 0) {
        if (contact->phone_count < MAX_PHONES_PER_CONTACT) {
            const char *colon = strchr(line, ':');
            if (colon) {
                char raw_number[MAX_PHONE_LEN];
                strncpy(raw_number, colon + 1, MAX_PHONE_LEN - 1);
                raw_number[MAX_PHONE_LEN - 1] = '\0';
                
                // Normalize the phone number
                normalize_phone_number(raw_number, 
                                     contact->phones[contact->phone_count].number,
                                     MAX_PHONE_LEN,
                                     g_country_code);
                
                // Extract type
                const char *type_start = strstr(line, "TYPE=");
                if (type_start) {
                    type_start += 5;
                    const char *type_end = strchr(type_start, ':');
                    if (!type_end) type_end = strchr(type_start, ';');
                    if (type_end) {
                        size_t len = type_end - type_start;
                        if (len >= 16) len = 15;
                        strncpy(contact->phones[contact->phone_count].type, 
                               type_start, len);
                        contact->phones[contact->phone_count].type[len] = '\0';
                    }
                } else {
                    strcpy(contact->phones[contact->phone_count].type, "OTHER");
                }
                
                contact->phone_count++;
            }
        }
    }
}

static esp_err_t parse_vcard(const char *vcard_text, contact_t *contact)
{
    memset(contact, 0, sizeof(contact_t));
    contact->active = true;
    
    char *line_start = (char*)vcard_text;
    char *line_end;
    
    while ((line_end = strstr(line_start, "\r\n")) != NULL) {
        *line_end = '\0';
        
        if (strlen(line_start) > 0) {
            parse_vcard_line(line_start, contact);
        }
        
        line_start = line_end + 2;
    }
    
    if (strlen(line_start) > 0) {
        parse_vcard_line(line_start, contact);
    }
    
    return ESP_OK;
}

static esp_err_t process_complete_vcards(phonebook_t *pb)
{
    char *search_start = pb->vcard_buffer;
    char *vcard_start;
    char *vcard_end;
    
    while ((vcard_start = strstr(search_start, "BEGIN:VCARD")) != NULL) {
        vcard_end = strstr(vcard_start, "END:VCARD");
        
        if (vcard_end == NULL) {
            size_t remaining = strlen(vcard_start);
            memmove(pb->vcard_buffer, vcard_start, remaining);
            pb->buffer_pos = remaining;
            pb->vcard_buffer[pb->buffer_pos] = '\0';
            return ESP_OK;
        }
        
        vcard_end += strlen("END:VCARD");
        char saved_char = *vcard_end;
        *vcard_end = '\0';
        
        contact_t temp_contact;
        if (parse_vcard(vcard_start, &temp_contact) == ESP_OK) {
            if (strlen(temp_contact.full_name) > 0 && temp_contact.phone_count > 0) {
                if (append_contact_to_file(pb, &temp_contact) == ESP_OK) {
                    pb->contact_count++;
                    
                    if (pb->contact_count % 50 == 0) {
                        ESP_LOGI(TAG, "Processed %d contacts", pb->contact_count);
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to write contact to file");
                }
            }
        }
        
        *vcard_end = saved_char;
        search_start = vcard_end;
    }
    
    if (strstr(pb->vcard_buffer, "BEGIN:VCARD") == NULL) {
        pb->buffer_pos = 0;
        pb->vcard_buffer[0] = '\0';
    }
    
    return ESP_OK;
}

esp_err_t phonebook_process_chunk(phonebook_t *pb, const char *data, uint16_t len)
{
    if (pb == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pb->buffer_pos + len >= VCARD_BUFFER_SIZE) {
        ESP_LOGE(TAG, "vCard buffer overflow, processing partial buffer");
        process_complete_vcards(pb);
        
        if (pb->buffer_pos + len >= VCARD_BUFFER_SIZE) {
            ESP_LOGE(TAG, "Buffer still full after processing");
            return ESP_ERR_NO_MEM;
        }
    }
    
    memcpy(pb->vcard_buffer + pb->buffer_pos, data, len);
    pb->buffer_pos += len;
    pb->vcard_buffer[pb->buffer_pos] = '\0';
    
    return process_complete_vcards(pb);
}

esp_err_t phonebook_finalize_sync(phonebook_t *pb)
{
    if (pb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    process_complete_vcards(pb);
    
    // Flush any remaining contacts in batch buffer
    flush_write_buffer(pb);
    
    esp_err_t err = update_contact_count_in_file(pb);
    
    pb->sync_in_progress = false;
    pb->buffer_pos = 0;
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Phonebook sync completed: %d contacts stored in flash", pb->contact_count);
    } else {
        ESP_LOGE(TAG, "Failed to finalize phonebook");
    }
    
    return err;
}

contact_t* phonebook_search_by_letter(phonebook_t *pb, char letter, uint16_t *count)
{
    if (pb == NULL || count == NULL) {
        return NULL;
    }
    
    letter = toupper(letter);
    *count = 0;
    
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        return NULL;
    }
    
    uint16_t total_count;
    fread(&total_count, sizeof(uint16_t), 1, f);
    
    for (int i = 0; i < total_count; i++) {
        contact_t temp_contact;
        if (fread(&temp_contact, sizeof(contact_t), 1, f) == 1) {
            if (temp_contact.active && toupper(temp_contact.full_name[0]) == letter) {
                (*count)++;
            }
        }
    }
    
    if (*count == 0) {
        fclose(f);
        return NULL;
    }
    
    contact_t *results = (contact_t*)malloc(sizeof(contact_t) * (*count));
    if (results == NULL) {
        fclose(f);
        *count = 0;
        return NULL;
    }
    
    fseek(f, sizeof(uint16_t), SEEK_SET);
    int idx = 0;
    for (int i = 0; i < total_count && idx < *count; i++) {
        contact_t temp_contact;
        if (fread(&temp_contact, sizeof(contact_t), 1, f) == 1) {
            if (temp_contact.active && toupper(temp_contact.full_name[0]) == letter) {
                memcpy(&results[idx++], &temp_contact, sizeof(contact_t));
            }
        }
    }
    
    fclose(f);
    return results;
}

contact_t* phonebook_search_by_name(phonebook_t *pb, const char *name, uint16_t *count)
{
    if (pb == NULL || name == NULL || count == NULL) {
        return NULL;
    }
    
    *count = 0;
    
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        return NULL;
    }
    
    uint16_t total_count;
    fread(&total_count, sizeof(uint16_t), 1, f);
    
    for (int i = 0; i < total_count; i++) {
        contact_t temp_contact;
        if (fread(&temp_contact, sizeof(contact_t), 1, f) == 1) {
            if (temp_contact.active && strcasestr(temp_contact.full_name, name) != NULL) {
                (*count)++;
            }
        }
    }
    
    if (*count == 0) {
        fclose(f);
        return NULL;
    }
    
    contact_t *results = (contact_t*)malloc(sizeof(contact_t) * (*count));
    if (results == NULL) {
        fclose(f);
        *count = 0;
        return NULL;
    }
    
    fseek(f, sizeof(uint16_t), SEEK_SET);
    int idx = 0;
    for (int i = 0; i < total_count && idx < *count; i++) {
        contact_t temp_contact;
        if (fread(&temp_contact, sizeof(contact_t), 1, f) == 1) {
            if (temp_contact.active && strcasestr(temp_contact.full_name, name) != NULL) {
                memcpy(&results[idx++], &temp_contact, sizeof(contact_t));
            }
        }
    }
    
    fclose(f);
    return results;
}

phone_number_t* phonebook_get_numbers(phonebook_t *pb, const char *full_name, uint8_t *count)
{
    if (pb == NULL || full_name == NULL || count == NULL) {
        return NULL;
    }
    
    *count = 0;
    
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        return NULL;
    }
    
    uint16_t total_count;
    fread(&total_count, sizeof(uint16_t), 1, f);
    
    for (int i = 0; i < total_count; i++) {
        contact_t temp_contact;
        if (fread(&temp_contact, sizeof(contact_t), 1, f) == 1) {
            if (temp_contact.active && strcmp(temp_contact.full_name, full_name) == 0) {
                *count = temp_contact.phone_count;
                
                if (*count == 0) {
                    fclose(f);
                    return NULL;
                }
                
                phone_number_t *numbers = (phone_number_t*)malloc(sizeof(phone_number_t) * (*count));
                if (numbers == NULL) {
                    fclose(f);
                    *count = 0;
                    return NULL;
                }
                
                memcpy(numbers, temp_contact.phones, sizeof(phone_number_t) * (*count));
                fclose(f);
                return numbers;
            }
        }
    }
    
    fclose(f);
    return NULL;
}

contact_t* phonebook_search_by_number(phonebook_t *pb, const char *number)
{
    if (pb == NULL || number == NULL) {
        return NULL;
    }
    
    // Normalize the search number
    char normalized_search[MAX_PHONE_LEN];
    normalize_phone_number(number, normalized_search, MAX_PHONE_LEN, g_country_code);
    
    char filepath[64];
    make_phonebook_path(pb->device_addr, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        return NULL;
    }
    
    uint16_t total_count;
    fread(&total_count, sizeof(uint16_t), 1, f);
    
    for (int i = 0; i < total_count; i++) {
        contact_t temp_contact;
        if (fread(&temp_contact, sizeof(contact_t), 1, f) != 1) {
            continue;
        }
        
        if (!temp_contact.active) continue;
        
        // Direct string comparison since all numbers are normalized
        for (int j = 0; j < temp_contact.phone_count; j++) {
            if (strcmp(temp_contact.phones[j].number, normalized_search) == 0) {
                contact_t *result = (contact_t*)malloc(sizeof(contact_t));
                if (result) {
                    memcpy(result, &temp_contact, sizeof(contact_t));
                }
                fclose(f);
                return result;
            }
        }
    }
    
    fclose(f);
    return NULL;
}

void phonebook_print_contact(contact_t *contact)
{
    if (contact == NULL) return;
    
    ESP_LOGI(TAG, "Contact: %s", contact->full_name);
    for (int i = 0; i < contact->phone_count; i++) {
        ESP_LOGI(TAG, "  Phone [%s]: %s", 
                contact->phones[i].type, contact->phones[i].number);
    }
}

uint16_t phonebook_get_count(phonebook_t *pb)
{
    if (pb == NULL) return 0;
    return pb->contact_count;
}
