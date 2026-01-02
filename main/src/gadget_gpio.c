#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gadget_gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "gadget_includes.h"
#include "gadget_gpio.h"

#include "driver/gpio.h"

const static char *gadget_tag = "gadget_mk1_gpio";

//defines
#define GPIO_TOTAL                    2
#define GADGET_LED_OUTPUT_IO_1        5 // Define the output GPIO
#define GADGET_LED_OUTPUT_IO_2        6 // Define the output GPIO

#define GADGET_GPIO_OUTPUT_PIN_SEL  ( (1ULL << GADGET_LED_OUTPUT_IO_1) | (1ULL << GADGET_LED_OUTPUT_IO_2) )
static esp_err_t gadget_init_gpio();

static bool gpio_init = false;

static bool gpio_states[GPIO_TOTAL] = { false };

/**
 * @brief gpio task
 * 
 * @param pvParams 
 */
void gadget_gpio_task(void *pvParams)
{
    static BaseType_t gStatus;
    static gadget_msg_t incoming_msg;
    static esp_err_t err;

    ESP_LOGI(gadget_tag, "Launching gadget gpio task");

    while(1)
    {
        gStatus = xQueueReceive(gadget_gpio_msg_queue, &incoming_msg, GADGET_MSG_SHORT_DELAY);
        
        if(gStatus == pdPASS)
        {
            switch(incoming_msg.msg_type)
            {
                case gadget_msg_init_gpio:
                    ESP_LOGI(gadget_tag, "initializing gpio");
                    if(!gpio_init)
                    {
                        err = gadget_init_gpio();
                        if(err != ESP_OK)
                        {
                            ESP_LOGE(gadget_tag, "ERROR init gpio!");
                            gpio_init = false;
                        }
                        else
                        {
                            //ESP_LOGI(gadget_tag, "PASS init gpio!");
                            gpio_init = true;
                        }
                    }
                    else
                        ESP_LOGI(gadget_tag, "gpio already initialized.");
                break;

                case gadget_msg_toggle_led_1:
                    if(gpio_init)
                    {
                        if(!gpio_states[0])
                        {
                            ESP_LOGI(gadget_tag, "LED 1 ON");
                            gpio_set_level(GADGET_LED_OUTPUT_IO_1, 1);
                            gpio_states[0] = true;
                        }
                        else
                        {
                            ESP_LOGI(gadget_tag, "LED 1 OFF");
                            gpio_set_level(GADGET_LED_OUTPUT_IO_1, 0);
                            gpio_states[0] = false;
                        }
                    }
                break;

                case gadget_msg_toggle_led_2:
                if(gpio_init)
                    {
                        if(!gpio_states[1])
                        {
                            ESP_LOGI(gadget_tag, "LED 2 ON");
                            gpio_set_level(GADGET_LED_OUTPUT_IO_2, 1);
                            gpio_states[1] = true;
                        }
                        else
                        {
                            ESP_LOGI(gadget_tag, "LED 2 OFF");
                            gpio_set_level(GADGET_LED_OUTPUT_IO_2, 0);
                            gpio_states[1] = false;
                        }
                    }
                break;

                default:
                    ESP_LOGW(gadget_tag, "UNKNOWN MESSAGE SENT TO GPIO %d", incoming_msg.msg_type);
                break;

            }
        }
    }

}


/**
 * @brief Initialize gadget gpio pins
 * 
 * GPIO 5 : LED
 * 
 * @return true 
 * @return false 
 */
static esp_err_t gadget_init_gpio()
{
    esp_err_t init = ESP_OK;
    gpio_config_t gadget_io_config = {};
    gadget_io_config.intr_type = GPIO_INTR_DISABLE;
    gadget_io_config.mode = GPIO_MODE_OUTPUT;
    gadget_io_config.pin_bit_mask = GADGET_GPIO_OUTPUT_PIN_SEL;
    gadget_io_config.pull_down_en = 1;
    gadget_io_config.pull_up_en = 0;

    init = gpio_config(&gadget_io_config);

    return init;
}