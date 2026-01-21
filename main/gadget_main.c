#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "includes/gadget_includes.h"
#include "includes/gadget_central.h"
#include "includes/gadget_gpio.h"
#include "includes/gadget_comms.h"

//Tag
const static char *gadget_tag = "gadget_mk1_main";

//Function Defines
static void ch_serial();
static esp_err_t init_tasks();
static esp_err_t init_msg_queues();

bool ap_init = false;
bool sta_init = false;

//FreeRTOS
QueueHandle_t gadget_central_msg_queue;
QueueHandle_t gadget_gpio_msg_queue;
QueueHandle_t gadget_comms_msg_queue;

//Variables
int8_t tick_count = 0;

/**
 * @brief check serial input 
 * 
 */
static void ch_serial()
{
    static char c;
    gadget_msg_t out_msg;

    c = fgetc(stdin);

    switch(c)
    {
        case 0xFF:
            break;

        case 'm':
            ESP_LOGI(gadget_tag, "Gadget Serial Input Menu:");
            ESP_LOGI(gadget_tag, "m - display menu");
            ESP_LOGI(gadget_tag, "1 - Toggle LED 1");
            ESP_LOGI(gadget_tag, "2 - Toggle LED 2");
            ESP_LOGI(gadget_tag, "a - create wifi ap");
            ESP_LOGI(gadget_tag, "s - create wifi sta");
            ESP_LOGI(gadget_tag, "p - ping");
        break;

        case '1':
            gadget_send_msg(gadget_central_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_main_id, gadget_msg_toggle_led_1, &out_msg);
        break;

        case '2':
            gadget_send_msg(gadget_central_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_main_id, gadget_msg_toggle_led_2, &out_msg);
        break;

        case 'a':
            gadget_send_msg(gadget_central_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_main_id, gadget_msg_init_wifi_ap, &out_msg);
        break;

        case 's':
            gadget_send_msg(gadget_central_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_main_id, gadget_msg_init_wifi_sta, &out_msg);
        break;

        case 'p':
            gadget_send_msg(gadget_central_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_main_id, gadget_msg_init_ping, &out_msg);
        break;

        default:
            //Nothing
            ESP_LOGW(gadget_tag, "Invalid char: %c", c);
        break;
    }
}


/**
 * @brief init FreeRTOS tasks
 * 
 * @return esp_err_t 
 */
static esp_err_t init_tasks()
{
    esp_err_t init = ESP_OK;
    BaseType_t xStatus;

    ESP_LOGI(gadget_tag, "-- INITIALIZING TASKS --");

    //central
    ESP_LOGI(gadget_tag, "creating gadget_central_task");
    xStatus = xTaskCreate(gadget_central_task, "gadget_central_task", (ESP32_BIT*96), NULL, GADGET_CENTRAL_TASK_PRIORITY, NULL);
    if(xStatus != pdPASS)
    {
        ESP_LOGE(gadget_tag, "ERROR with creation of central msg TASK!");
        init = ESP_FAIL;
    }

    ESP_LOGI(gadget_tag, "creating gadget_gpio_task");
    xStatus = xTaskCreate(gadget_gpio_task, "gadget_gpio_task", (ESP32_BIT*96), NULL, GADGET_GPIO_TASK_PRIORITY, NULL);
    if(xStatus != pdPASS)
    {
        ESP_LOGE(gadget_tag, "ERROR with creation of gpio msg TASK!");
        init = ESP_FAIL;
    }

    ESP_LOGI(gadget_tag, "creating gadget_comms_task");
    xStatus = xTaskCreate(gadget_comms_task, "gadget_comms_task", (ESP32_BIT*128), NULL, GADGET_COMMS_TASK_PRIORITY, NULL);
    if(xStatus != pdPASS)
    {
        ESP_LOGE(gadget_tag, "ERROR with creation of comms msg TASK!");
        init = ESP_FAIL;
    }

    return init;
}

static esp_err_t init_msg_queues()
{
    bool init = ESP_OK;

    ESP_LOGI(gadget_tag, "-- INITIALIZING MESSAGE QUEUES --");

    //central
    ESP_LOGI(gadget_tag, "creating central msg queue of size %d", GADGET_CENTRAL_Q_SIZE);
    gadget_central_msg_queue = xQueueCreate(GADGET_CENTRAL_Q_SIZE, sizeof(gadget_msg_t));
    if(gadget_central_msg_queue == NULL) 
    {
        ESP_LOGE(gadget_tag, "ERROR with creation of central msg QUEUE!");
        init = ESP_FAIL;
    }

    //gpio
    ESP_LOGI(gadget_tag, "creating gpio msg queue of size %d", GADGET_GPIO_Q_SIZE);
    gadget_gpio_msg_queue = xQueueCreate(GADGET_GPIO_Q_SIZE, sizeof(gadget_msg_t));
    if(gadget_gpio_msg_queue ==  NULL) 
    {
        ESP_LOGE(gadget_tag, "ERROR with creation of gpio msg queue!");
        init = ESP_FAIL;
    }

    //comms
    ESP_LOGI(gadget_tag, "creating comms msg queue of size %d", GADGET_COMMS_Q_SIZE);
    gadget_comms_msg_queue = xQueueCreate(GADGET_COMMS_Q_SIZE, sizeof(gadget_msg_t));
    if(gadget_comms_msg_queue ==  NULL) 
    {
        ESP_LOGE(gadget_tag, "ERROR with creation of comms msg queue!");
        init = ESP_FAIL;
    }

    return init;
}

void app_main(void)
{
    static uint8_t boot_seq = 0;

    esp_err_t run = ESP_OK;

    gadget_msg_t out_msg;

    //Initialize NVS
    ESP_LOGI(gadget_tag, "-- INITIALIZING NVS --");
    run = nvs_flash_init();
    if (run == ESP_ERR_NVS_NO_FREE_PAGES || run == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        run = nvs_flash_init();
    }
    if(run == ESP_OK) boot_seq = 1;

    //init IO
    run = init_msg_queues();
    if(run == ESP_OK) boot_seq = 2;

    run = init_tasks();
    if(run == ESP_OK) boot_seq = 3;


    //Send off messages
    gadget_send_msg(gadget_central_msg_queue, GADGET_MSG_SHORT_DELAY, gadget_tag, gadget_main_id, gadget_msg_init_gpio, &out_msg);

    while(run == ESP_OK)
    {
        ch_serial();
        vTaskDelay(GADGET_TICK_RATE/portTICK_PERIOD_MS);
    }
    
    if(run != ESP_OK)
    {
        ESP_LOGI(gadget_tag, "BOOT has failed at boot_seq: %d", boot_seq);
    }
}

/**
 * @brief compile and offload msg
 * 
 * @param msg_queue 
 * @param ticks_wait 
 * @param sender 
 * @param msg_sender 
 * @param msg 
 */
BaseType_t gadget_send_msg(QueueHandle_t msg_queue, 
                    TickType_t ticks_to_wait, 
                    const char* sender, 
                    msg_sender_t msg_sender, 
                    msg_type_t msg_type, 
                    gadget_msg_t *msg)
{
    //sender to logging
    BaseType_t xStatus;
    msg->msg_sender = msg_sender;
    msg->msg_type = msg_type;
    xStatus = xQueueSendToBack(msg_queue, msg, ticks_to_wait);
    if(xStatus != pdPASS)
    {
        ESP_LOGE("gadget_msg_sender", "msg queue (%d) FULL!", msg_sender);
    }

    return(xStatus);
}


