#include "pci.h"
#include "io.h"

static uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return (uint32_t)(1u << 31)
         | ((uint32_t)bus << 16)
         | ((uint32_t)slot << 11)
         | ((uint32_t)func << 8)
         | (off & 0xFCu);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, off));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, slot, func, off);
    return (uint16_t)(v >> ((off & 2) * 8));
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, slot, func, off);
    return (uint8_t)(v >> ((off & 3) * 8));
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, off));
    outl(PCI_CONFIG_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint16_t val) {
    uint32_t v = pci_read32(bus, slot, func, off);
    int shift = (off & 2) * 8;
    v &= ~(0xFFFFu << shift);
    v |= ((uint32_t)val << shift);
    pci_write32(bus, slot, func, off, v);
}

void pci_enable_busmaster(const pci_dev_t *d) {
    uint16_t cmd = pci_read16(d->bus, d->slot, d->func, 0x04);
    cmd |= (1u << 2) | (1u << 0); /* bus master + io space */
    pci_write16(d->bus, d->slot, d->func, 0x04, cmd);
}

int pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read16(bus, slot, 0, 0x00);
            if (v == 0xFFFF) continue;
            uint8_t hdr = pci_read8(bus, slot, 0, 0x0E);
            uint8_t funcs = (hdr & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < funcs; func++) {
                uint16_t dv = pci_read16(bus, slot, func, 0x00);
                if (dv == 0xFFFF) continue;
                uint16_t dd = pci_read16(bus, slot, func, 0x02);
                if (dv != vendor || dd != device) continue;
                out->bus = (uint8_t)bus;
                out->slot = slot;
                out->func = func;
                out->vendor = dv;
                out->device = dd;
                out->class_id = pci_read8(bus, slot, func, 0x0B);
                out->subclass = pci_read8(bus, slot, func, 0x0A);
                out->prog_if  = pci_read8(bus, slot, func, 0x09);
                out->irq_line = pci_read8(bus, slot, func, 0x3C);
                for (int i = 0; i < 6; i++)
                    out->bar[i] = pci_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
                return 1;
            }
        }
    }
    return 0;
}
