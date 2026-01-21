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

#include "ping/ping_sock.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

const static char *gadget_tag = "gadget_mk1_sta";

static EventGroupHandle_t sta_wifi_event_group;

static bool sta_init_in = false;
static bool ping_init = false;

static esp_ping_handle_t ping;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *) event_data;
        ESP_LOGI(gadget_tag, "Station %.s joined, AID=%d",
                 event->ssid_len, event->ssid, event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *) event_data;
        ESP_LOGI(gadget_tag, "Station %.s left, reason=%d",
                 event->ssid_len, event->ssid, event->reason);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
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
            .ssid = "mod1",
            .password = "!Temp101",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = 5,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config);
    if(err != ESP_OK)
    {
        
    }

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
bool gadget_sta_init(char *ssid, char *pwd)
{
    esp_err_t ret = ESP_OK;
    ret = esp_netif_init();
    ret = esp_event_loop_create_default();

    //Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        ret = nvs_flash_init();
        ESP_LOGE(gadget_tag, "STA: Error Initializing NVS");
    }

    sta_wifi_event_group = xEventGroupCreate();

    // Initialize Event handler for WIFI STA
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL);
    
    if(ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "ERROR: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize Event handler for IP Aqcuisition
    ret = esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL);
    
    if(ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "ERROR: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize esp-idf's wifi's default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if(ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "ERROR: %s", esp_err_to_name(ret));
        return false;
    }

    // Set to explicitly station mode
    ret = esp_wifi_set_mode(WIFI_MODE_STA);

    if(ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "ERROR: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(gadget_tag, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = gadget_init_sta_interface(ssid, pwd);

    // Start Connection to wifi.
    ret = esp_wifi_start();
    if(ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "esp_wifi_start ERROR: %s", esp_err_to_name(ret));
        return false;
    }

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
        return false;
    }

    /* Set sta as the default interface */
    ret = esp_netif_set_default_netif(esp_netif_sta);
    if(ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "esp_netif_set_default_netif ERROR: %s", esp_err_to_name(ret));
        return false;
    }
    sta_init_in = true;
    return true;
}

static void ping_success(esp_ping_handle_t ping_hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_ip;

    //esp_ping profiles for each type of ping profile
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_IPADDR, &target_ip, sizeof(target_ip));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    
    ESP_LOGI(gadget_tag, "%d bytes from ping %s, seqno=%d, ttl=%d, elapsed time=%d",
                        recv_len, inet_ntoa(target_ip.u_addr.ip4), seqno, ttl, elapsed_time);
    
}

static void ping_timeout(esp_ping_handle_t ping_hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_ip;
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_IPADDR, &target_ip, sizeof(target_ip));

    ESP_LOGI(gadget_tag, "seqno=%d, ping to %s timeout", seqno, inet_ntoa(target_ip.u_addr.ip4));
}

static void ping_end(esp_ping_handle_t ping_hdl, void *args)
{
    uint32_t transmitted;
    uint32_t total_recv;
    uint32_t total_uptime;

    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_REPLY, &total_recv, sizeof(total_recv));
    esp_ping_get_profile(ping_hdl, ESP_PING_PROF_DURATION, &total_uptime, sizeof(total_uptime));

    ESP_LOGI(gadget_tag, "ping session ended, total transmitted=%d, total recv=%d, total uptime:%d", transmitted, total_recv, total_uptime);
}

/**
 * @brief Ping pong
 * 
 * @return true 
 * @return false 
 */
bool gadget_init_ping(char *url)
{
    if(!sta_init_in)
    {
        ESP_LOGW(gadget_tag, "STA not initialized!");
        return false;
    }
    ip_addr_t target_ip;
    //struct from lwip
    //URL's metadata return
    struct addrinfo hint; 
    struct addrinfo *res = NULL; 
    //initializes
    memset(&target_ip, 0x0, sizeof(target_ip));
    memset(&hint, 0x0, sizeof(hint));

    //retrieve IP address for URL
    getaddrinfo("www.google.com", NULL, &hint, &res);
    struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_ip), &addr4);
    //free resulting address from getaddrinfo
    freeaddrinfo(res);

    //esp's ping configuration
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_ip;
    ping_config.count = ESP_PING_COUNT_INFINITE;

    esp_ping_callbacks_t ping_callbacks;
    ping_callbacks.on_ping_success = ping_success;
    ping_callbacks.on_ping_timeout = ping_timeout;
    ping_callbacks.on_ping_end = ping_end;

    esp_err_t ret = ESP_OK;
    if(!ping_init)
    {
        ESP_LOGI(gadget_tag, "Initializing ping session");
        ret = esp_ping_new_session(&ping_config, &ping_callbacks, &ping);
        ESP_LOGI(gadget_tag, "Starting ping session.");
        ret = esp_ping_start(ping);
        if(ret != ESP_OK)
        {
            ESP_LOGE(gadget_tag, "ERROR: gadget_init_ping, failed to establish new or start new session.");
            return false;
        }
        ping_init = true;
    }
    ESP_LOGI(gadget_tag, "Ping session initialized.");
    return true;
}

bool gadget_stop_ping()
{
    esp_err_t ret = ESP_OK;
    if(ping_init)
    {
        ESP_LOGI(gadget_tag, "Stopping ping session");
        ret = esp_ping_stop(ping); 
        ret = esp_ping_delete_session(ping);
        if(ret!=ESP_OK)
        {
            ESP_LOGE(gadget_tag, "ERROR stop_ping, cannot stop or delete ping session.");
        }
        ping_init = false;
    }
    else
    {
        ESP_LOGE(gadget_tag, "ERROR stop_ping, no ping session.");
        return false;
    }
    return true;
}


