#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"

#include "gadget_includes.h"
#include "gadget_central.h"

const static char *gadget_tag = "gadget_mk1_central";

/**
 * @brief central task
 * 
 * @param pvParams 
 */
void gadget_central_task(void *pvParams)
{
    static BaseType_t gStatus;
    static gadget_msg_t incoming_msg;

    ESP_LOGI(gadget_tag, "Launching gadget central");

    while(1)
    {
        gStatus = xQueueReceive(gadget_central_msg_queue, &incoming_msg, GADGET_MSG_SHORT_DELAY);
        
        if(gStatus == pdPASS)
        {
            switch(incoming_msg.msg_type)
            {
                case gadget_msg_init_gpio:
                    ESP_LOGI(gadget_tag, "Sending msg to init_gpio");
                    gadget_send_msg(gadget_gpio_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_central_id, incoming_msg.msg_type, &incoming_msg);
                break;

                case gadget_msg_toggle_led_1:
                    ESP_LOGI(gadget_tag, "Sending msg to blink led");
                    gadget_send_msg(gadget_gpio_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_central_id, gadget_msg_toggle_led_1, &incoming_msg);
                break;

                case gadget_msg_toggle_led_2:
                    ESP_LOGI(gadget_tag, "Sending msg to blink led");
                    gadget_send_msg(gadget_gpio_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_central_id, gadget_msg_toggle_led_2, &incoming_msg);
                break;

                case gadget_msg_init_wifi_ap:
                    ESP_LOGI(gadget_tag, "Sending msg to init wifi ap");
                    gadget_send_msg(gadget_comms_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_central_id, gadget_msg_init_wifi_ap, &incoming_msg);
                break;

                case gadget_msg_send_text:
                    ESP_LOGI(gadget_tag, "Sending msg of send msg thru websocket");
                    gadget_send_msg(gadget_comms_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_central_id, gadget_msg_send_text, &incoming_msg);
                break;

                default:
                    ESP_LOGW(gadget_tag, "UNKNOWN MESSAGE SENT TO CENTRAL %d", incoming_msg.msg_type);
                break;

            }
        }
    }

}