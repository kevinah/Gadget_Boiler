#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"

#include "gadget_includes.h"
#include "gadget_comms.h"
#include "gadget_ap.h"
#include "gadget_sta.h"

const static char *gadget_tag = "gadget_mk1_comms";

/**
 * @brief central task
 * 
 * @param pvParams 
 */
void gadget_comms_task(void *pvParams)
{
    static BaseType_t gStatus;
    static gadget_msg_t incoming_msg;

    static bool ap_init = false;

    ESP_LOGI(gadget_tag, "Launching gadget comms");

    while(1)
    {
        gStatus = xQueueReceive(gadget_comms_msg_queue, &incoming_msg, GADGET_MSG_SHORT_DELAY);
        
        if(gStatus == pdPASS)
        {
            switch(incoming_msg.msg_type)
            {
                case gadget_msg_init_wifi_ap:
                    ESP_LOGI(gadget_tag, "initializing ap");
                    gadget_ap_init();
                    ap_init = start_ws();
                break;
                case gadget_msg_init_wifi_sta:
                    ESP_LOGI(gadget_tag, "initializing sta");
                    gadget_sta_init("", ""); // ** FILL
                break;
                
                case gadget_msg_send_text:
                    if(ap_init)
                        gadget_send_text_ws("TEST");
                    else
                        ESP_LOGI(gadget_tag, "AP not initialized!");
                break;

                case gadget_msg_init_ping:
                    ESP_LOGI(gadget_tag, "initializing ping");
                break;

                default:
                    ESP_LOGW(gadget_tag, "UNKNOWN MESSAGE SENT TO CENTRAL %d", incoming_msg.msg_type);
                break;

            }
        }
    }

}