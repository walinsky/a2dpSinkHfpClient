#ifndef __BT_HFP_H__
#define __BT_HFP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_hf_client_api.h"

/**
 * @brief     callback function for HF client
 */
void bt_app_hfp_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param);

#ifdef __cplusplus
}
#endif

#endif /* __BT_HFP_H__*/
