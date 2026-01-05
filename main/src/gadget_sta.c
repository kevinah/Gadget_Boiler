#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gadget_includes.h"
#include "gadget_sta.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

const static char *gadget_tag = "gadget_mk1_sta";

static EventGroupHandle_t sta_wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(gadget_tag, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(gadget_tag, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(sta_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief initialize sta of gadget with given ssid and pwd
 * 
 * @param ssid 
 * @param pwd 
 * @return esp_netif_t* 
 */
esp_netif_t *gadget_init_sta_interface(char *ssid, char *pwd)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = {(unsigned char)*ssid},
            .password = {(unsigned char)*pwd},
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = 5,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(gadget_tag, "wifi_init_sta finished.");

    return esp_netif_sta;
}

/**
 * @brief 
 * 
 * @note ESP32-S3 is ONLY rated for 2.4GHz bands.
 * 
 * @param ssid 
 * @param pwd 
 */
void gadget_sta_init(char *ssid, char *pwd)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    sta_wifi_event_group = xEventGroupCreate();

    // Event handler for WIFI STA
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    // Event handler for IP Aqcuisition
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    // Initialize esp-idf's wifi's default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set to explicitly station mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(gadget_tag, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = gadget_init_sta_interface(ssid, pwd);

    // Start Connection to wifi.
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(sta_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(gadget_tag, "connected to ap SSID:%s password:%s",
                 ssid, pwd);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(gadget_tag, "Failed to connect to SSID:%s, password:%s",
                 ssid, pwd);
    } else {
        ESP_LOGE(gadget_tag, "UNEXPECTED EVENT");
        return;
    }

    /* Set sta as the default interface */
    esp_netif_set_default_netif(esp_netif_sta);

}
