
set(GADGET_INCLUDES
    "./includes/"
)
set(GADGET_TASKS
    "./src/gadget_central.c"
    "./src/gadget_gpio.c"
    "./src/gadget_comms.c"
)

set(GADGET_SRC
    "./src/gadget_ap.c"
    "./src/gadget_sta.c"
    "./src/gadget_log.c"
)

set(GADGET_COMPONENTS
    littlefs
    lwip
    esp_wifi
    esp_timer
    nvs_flash
    esp_http_server
    esp_driver_gpio
)

idf_component_register( SRCS "gadget_main.c" 
                            ${GADGET_TASKS} 
                            ${GADGET_SRC}
                        INCLUDE_DIRS "." 
                            ${GADGET_INCLUDES}
                        REQUIRES 
                            ${GADGET_COMPONENTS}
)
