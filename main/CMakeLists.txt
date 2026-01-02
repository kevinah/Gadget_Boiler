
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
)

set(GADGET_COMPONENTS

)

idf_component_register( SRCS "gadget_main.c" 
                            ${GADGET_TASKS} 
                            ${GADGET_SRC}
                        INCLUDE_DIRS "." 
                            ${GADGET_INCLUDES}
                        REQUIRES 
                            ${GADGET_COMPONENTS}
)
