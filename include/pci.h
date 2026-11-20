#ifndef SP_PCI_H
#define SP_PCI_H

#include "types.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

typedef struct {
    uint8_t bus, slot, func;
    uint16_t vendor, device;
    uint8_t class_id, subclass, prog_if, irq_line;
    uint32_t bar[6];
} pci_dev_t;

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint16_t val);

int pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out);
void pci_enable_busmaster(const pci_dev_t *d);

#endif
