#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gadget_includes.h"
#include "gadget_ap.h"

#include "esp_log.h"
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

#include "esp_http_server.h"

const static char *gadget_tag = "gadget_mk1_ap";

#define GADGET_AP_SSID          "gadget-ap-module"
#define GADGET_AP_PASSWORD      "SupremeDream9055"
#define GADGET_AP_WIFI_CHANNEL  1
#define GADGET_AP_MAX_CONN      1

#define WIFI_CONN_BIT           BIT0
#define WIFI_FAIL_BIT           BIT1

static esp_err_t gadget_start_websocket();
static void gadget_async_send(void *arg);
static esp_err_t gadget_send_over_ws(httpd_handle_t handle, const char *payload);
static esp_err_t async_ws_handler(httpd_req_t *request);

httpd_handle_t gadget_global_server;
int websocket_fd;

bool start_ws()
{
    esp_err_t init = ESP_OK;
    init = gadget_start_websocket();
    if(init != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "ERROR Failed to start websocket CODE(%s)", esp_err_to_name(init));
        return false;
    }
    ESP_LOGI(gadget_tag, "Started Websocket Server!");
    return true;
}
//websocket uri
static const httpd_uri_t ws = {
        .uri = "/ws",
        .method     = HTTP_GET,
        .handler    = async_ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
};

//wifi ap
static EventGroupHandle_t ap_wifi_event_group;

/**
 * @brief Wifi AP event handler
 * 
 * @param arg 
 * @param event_base 
 * @param event_id 
 * @param event_data 
 */
static void ap_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(gadget_tag, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(gadget_tag, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

/**
 * @brief initialize network interface for SoftAP
 * 
 * @return esp_netif_t* 
 */
esp_netif_t *gadget_ap_init_config()
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = GADGET_AP_SSID,
            .ssid_len = strlen(GADGET_AP_SSID),
            .channel = GADGET_AP_WIFI_CHANNEL,
            .password = GADGET_AP_PASSWORD,
            .max_connection = GADGET_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(gadget_tag, "gadget_ap_init SSID:%s password:%s channel:%d",
             GADGET_AP_SSID, GADGET_AP_PASSWORD, GADGET_AP_WIFI_CHANNEL);

    return esp_netif_ap;
}

/**
 * @brief initialize ap & websocket
 * 
 */
void gadget_ap_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ap_wifi_event_group = xEventGroupCreate();

    //Register WIFI_EVENT
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &ap_event_handler,
                    NULL,
                    NULL));

    //init wifi config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_LOGI(gadget_tag, "esp_wifi initializing ap");
    esp_netif_t *gadget_netif_ap = gadget_ap_init_config();

    ESP_ERROR_CHECK(esp_wifi_start());
}

//WEBSOCKET
//Asynchronous response data structure
struct async_resp_arg
{
    httpd_handle_t hd; // Server instance
    int fd;            // Session socket file descriptor
    char *payload;
};

/**
 * @brief generate asynchronous response
 * 
 * @param arg 
 */
static void gadget_async_send(void *arg)
{
    // Initialize asynchronous response data structure
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)arg;
    //create websocket packet
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)resp_arg->payload;
    ws_pkt.len = strlen(resp_arg->payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    //send
    httpd_ws_send_frame_async(resp_arg->hd, resp_arg->fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t gadget_send_over_ws(httpd_handle_t handle, const char *payload)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (resp_arg == NULL) {
        return ESP_ERR_NO_MEM;
    }
    resp_arg->hd = handle;
    resp_arg->fd = websocket_fd;
    resp_arg->payload = payload;
    ESP_LOGW(gadget_tag, "fd: %d", resp_arg->fd);
    esp_err_t ret = httpd_queue_work(handle, gadget_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}

/**
 * @brief Asychronous Websocket handler
 * 
 * @param request 
 * @return esp_err_t 
 */
static esp_err_t async_ws_handler(httpd_req_t *request)
{
    //Check websocket request for HTTP_GET validity
    //ESP_LOGI("async","request->handle: %d", request->handle);
    if(request->method == HTTP_GET)
    {
        websocket_fd = httpd_req_to_sockfd(request);
        ESP_LOGI(gadget_tag, "Websocket Connection Established | fd: %d", websocket_fd);
        return ESP_OK;
    }

    esp_err_t ws_ret;
    //Handle websocket packet
    httpd_ws_frame_t ws_pkt;
    uint8_t *data_buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    //set 0 to get frame length
    ws_ret = httpd_ws_recv_frame(request, &ws_pkt, 0);
    if(ws_ret != ESP_OK)
    {
        ESP_LOGE(gadget_tag, "ERROR httpd_ws_recv_frame(1) failed! CODE(%s)", esp_err_to_name(ws_ret) );
        return ws_ret;
    }
    //display frame length
    ESP_LOGI(gadget_tag, "websocket packet length: %d", ws_pkt.len);
    if(ws_pkt.len)
    {
        //string based comm from ws, add 1 additional space for \0 char
        data_buf = calloc(1, ws_pkt.len + 1);
        if(data_buf == NULL)
        {
            ESP_LOGE(gadget_tag, "ERROR failed to calloc onto ws data_buf!");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = data_buf;

        //retrieve frame
        ws_ret = httpd_ws_recv_frame(request, &ws_pkt, ws_pkt.len);
        if(ws_ret != ESP_OK)
        {
            ESP_LOGE(gadget_tag, "ERROR http_ws_recv_frame(2) failed! CODE(%s)", esp_err_to_name(ws_ret));
            free(data_buf);
            return ws_ret;
        }
        ESP_LOGI(gadget_tag, "received packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(gadget_tag, "ws_pkt.type: %d", ws_pkt.type);

    //PROCESS INCOMING DATA HERE FOR
    
    free(data_buf);

    return ESP_OK;
}

/**
 * @brief initialize and start websocket
 * 
 * @return esp_err_t 
 */
static esp_err_t gadget_start_websocket()
{
    esp_err_t init = ESP_FAIL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    
    ESP_LOGI(gadget_tag, "attempting to start websocket server on port: %d", cfg.server_port);
    if(httpd_start(&gadget_global_server, &cfg) == ESP_OK)
    {
        ESP_LOGI(gadget_tag, "Registering URI handlers");
        init = httpd_register_uri_handler(gadget_global_server, &ws);
        return init;
    }
    ESP_LOGE(gadget_tag, "ERROR failed to start websocket server");

    return init;
}

/**
 * @brief send text over wifi
 * 
 * @param payload 
 * @return true 
 * @return false 
 */
bool gadget_send_text_ws(const char *payload)
{
    esp_err_t sent = ESP_OK;
    sent = gadget_send_over_ws(gadget_global_server, payload);
    if(sent != ESP_OK)
    {
        ESP_LOGI(gadget_tag, "ERROR failed to send message over websocket! CODE(%s)", esp_err_to_name(sent));
        return false;
    }
    ESP_LOGI(gadget_tag, "Sending payload: %s", payload);
    return true;
}

