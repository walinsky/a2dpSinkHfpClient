#ifndef PHONEBOOK_H
#define PHONEBOOK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_bt_defs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NAME_LEN 64
#define MAX_PHONE_LEN 32
#define MAX_PHONES_PER_CONTACT 5
#define VCARD_BUFFER_SIZE 4096
#define CONTACT_BATCH_SIZE 20
#define DEFAULT_COUNTRY_CODE "31"  // Netherlands - change as needed

typedef struct {
    char number[MAX_PHONE_LEN];
    char type[16];
} phone_number_t;

typedef struct {
    char full_name[MAX_NAME_LEN];
    phone_number_t phones[MAX_PHONES_PER_CONTACT];
    uint8_t phone_count;
    bool active;
} contact_t;

typedef struct {
    esp_bd_addr_t device_addr;
    uint16_t contact_count;
    char vcard_buffer[VCARD_BUFFER_SIZE];
    uint16_t buffer_pos;
    bool sync_in_progress;
    contact_t *write_buffer;
    uint16_t write_buffer_count;
} phonebook_t;

typedef struct phonebook_list_node {
    phonebook_t phonebook;
    struct phonebook_list_node *next;
} phonebook_list_node_t;

// Function prototypes
esp_err_t phonebook_init(void);
void phonebook_set_country_code(const char *country_code);
phonebook_t* phonebook_get_or_create(esp_bd_addr_t device_addr);
phonebook_t* phonebook_find(esp_bd_addr_t device_addr);
esp_err_t phonebook_delete(esp_bd_addr_t device_addr);
esp_err_t phonebook_process_chunk(phonebook_t *pb, const char *data, uint16_t len);
esp_err_t phonebook_finalize_sync(phonebook_t *pb);
contact_t* phonebook_search_by_letter(phonebook_t *pb, char letter, uint16_t *count);
contact_t* phonebook_search_by_name(phonebook_t *pb, const char *name, uint16_t *count);
phone_number_t* phonebook_get_numbers(phonebook_t *pb, const char *full_name, uint8_t *count);
contact_t* phonebook_search_by_number(phonebook_t *pb, const char *number);
void phonebook_print_contact(contact_t *contact);
uint16_t phonebook_get_count(phonebook_t *pb);

#ifdef __cplusplus
}
#endif

#endif // PHONEBOOK_H
