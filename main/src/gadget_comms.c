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
    static bool sta_init = false;
    static bool ping_init = false;

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
                    if(!sta_init)
                        sta_init = gadget_sta_init("", ""); // ** FILL
                    else
                        ESP_LOGW(gadget_tag, "sta already initialized.");
                break;

                case gadget_msg_init_ping:
                    if(!ping_init)
                    {
                        ESP_LOGI(gadget_tag, "starting ping.");
                        ping_init = gadget_init_ping();
                    }
                    else
                    {
                        ESP_LOGW(gadget_tag, "stopping ping.");
                        ping_init = !gadget_stop_ping();
                    }
                break;

                default:
                    ESP_LOGW(gadget_tag, "UNKNOWN MESSAGE SENT TO CENTRAL %d", incoming_msg.msg_type);
                break;

            }
        }
    }

}