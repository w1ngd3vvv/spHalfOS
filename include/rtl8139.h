#ifndef SP_RTL8139_H
#define SP_RTL8139_H

#include "types.h"

void rtl8139_init(void);
int  rtl8139_present(void);
void rtl8139_get_mac(uint8_t mac[6]);
int  rtl8139_send(const void *data, uint32_t len);
int  rtl8139_recv(void *buf, uint32_t maxlen);

#endif
