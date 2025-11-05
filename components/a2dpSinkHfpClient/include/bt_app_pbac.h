#ifndef __BT_APP_PBAC_H__
#define __BT_APP_PBAC_H__

#include <stdint.h>
#include "esp_pbac_api.h"
#include "phonebook.h"

void bt_app_pbac_cb(esp_pbac_event_t event, esp_pbac_param_t *param);
void bt_app_pbac_task_start(void);  // New: start the phonebook processing task

// Public search functions
phonebook_t* bt_app_pbac_get_current_phonebook(void);
void bt_app_pbac_search_contacts(const char *query);
void bt_app_pbac_list_contacts_by_letter(char letter);
contact_t* bt_app_pbac_find_by_number(const char *number);
// Add these functions
void bt_app_pbac_pause(void);
void bt_app_pbac_resume(void);

#endif /* __BT_APP_PBAC_H__ */
