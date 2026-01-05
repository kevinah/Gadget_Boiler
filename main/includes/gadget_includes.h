#ifndef GADGET_INCLUDES_H
#define GADGET_INCLUDES_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define ESP32_BIT                   32

#define GADGET_TICK_RATE            100

#define GADGET_MSG_SHORT_DELAY      1000
#define GADGET_MSG_LONG_DELAY       5000

#define GADGET_MSG_DATA_SIZE        10

#define GADGET_CENTRAL_TASK_PRIORITY  5
#define GADGET_CENTRAL_Q_SIZE         10

#define GADGET_GPIO_TASK_PRIORITY     1
#define GADGET_GPIO_Q_SIZE            1

#define GADGET_COMMS_TASK_PRIORITY     4
#define GADGET_COMMS_Q_SIZE            2

//FreeRTOS
extern QueueHandle_t gadget_central_msg_queue;
extern QueueHandle_t gadget_gpio_msg_queue;
extern QueueHandle_t gadget_comms_msg_queue;

extern bool ap_init;
extern bool sta_init;

//typedef & structs
typedef enum {
    gadget_main_id,
    gadget_central_id,
    gadget_comms_id,
} msg_sender_t;

typedef enum {
    gadget_msg_init_gpio,
    gadget_msg_toggle_led_1,
    gadget_msg_toggle_led_2,
    gadget_msg_init_wifi_ap,
    gadget_msg_init_wifi_sta,
    gadget_msg_send_text,
    gadget_msg_init_ping
} msg_type_t;

typedef struct {
    msg_sender_t msg_sender;
    msg_type_t msg_type;
    uint8_t data[GADGET_MSG_DATA_SIZE];
} gadget_msg_t;

//functions
BaseType_t gadget_send_msg(QueueHandle_t msg_queue,
                    TickType_t ticks_to_wait, 
                    const char* sender, 
                    msg_sender_t msg_sender, 
                    msg_type_t msg_type, 
                    gadget_msg_t *msg);




#endif