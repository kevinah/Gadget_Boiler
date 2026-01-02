#ifndef GADGET_AP_H
#define GADGET_AP_H

void gadget_ap_init();
bool start_ws();
bool gadget_send_text_ws(const char* payload);

#endif