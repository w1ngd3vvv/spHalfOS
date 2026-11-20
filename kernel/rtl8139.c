#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "mm.h"
#include "idt.h"
#include "kernel.h"
#include "string.h"

#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139

#define REG_MAC0   0x00
#define REG_MAR0   0x08
#define REG_TSD0   0x10
#define REG_TSAD0  0x20
#define REG_RBSTART 0x30
#define REG_CMD    0x37
#define REG_CAPR   0x38
#define REG_CBR    0x3A
#define REG_IMR    0x3C
#define REG_ISR    0x3E
#define REG_RCR    0x44
#define REG_CONFIG1 0x52

#define RX_BUF_SIZE (8192 + 16 + 1500)
#define RX_BUF_ALLOC 12288u
#define TX_BUF_SIZE 1536u

#define PKT_QUEUE 32
#define PKT_MAX   1600

typedef struct {
    uint16_t len;
    uint8_t data[PKT_MAX];
} pkt_t;

static int present;
static uint32_t io_base;
static uint8_t mac[6];

static uint8_t *rx_buf;
static uint32_t rx_phys;
static uint32_t rx_off; /* read offset, CAPR-relative */

static uint8_t *tx_buf[4];
static uint32_t tx_phys[4];
static int tx_cur;

static pkt_t rxq[PKT_QUEUE];
static volatile int rx_head, rx_tail;

static inline uint32_t rx_phys_addr(void) { return rx_phys; }

static uint32_t alloc_contig_pages(uint32_t pages) {
    /* simple: allocate pages and ensure contiguous; if not, keep trying a few times */
    uint32_t first[8];
    if (pages == 0 || pages > 8) return 0;
    for (int attempt = 0; attempt < 64; attempt++) {
        first[0] = pmm_alloc_frame();
        if (!first[0]) return 0;
        int ok = 1;
        for (uint32_t i = 1; i < pages; i++) {
            first[i] = pmm_alloc_frame();
            if (!first[i]) { ok = 0; break; }
            if (first[i] != first[i - 1] + PAGE_SIZE) { ok = 0; break; }
        }
        if (ok) return first[0];
        /* free this run and try again */
        for (uint32_t i = 0; i < pages; i++) {
            if (first[i]) pmm_free_frame(first[i]);
            first[i] = 0;
        }
    }
    return 0;
}

static void rxq_push(const uint8_t *data, uint16_t len) {
    int next = (rx_head + 1) % PKT_QUEUE;
    if (next == rx_tail) return; /* drop */
    if (len > PKT_MAX) len = PKT_MAX;
    pkt_t *p = &rxq[rx_head];
    p->len = len;
    memcpy(p->data, data, len);
    rx_head = next;
}

static int rxq_pop(void *buf, uint32_t maxlen) {
    if (rx_tail == rx_head) return 0;
    pkt_t *p = &rxq[rx_tail];
    uint32_t n = p->len;
    if (n > maxlen) n = maxlen;
    memcpy(buf, p->data, n);
    rx_tail = (rx_tail + 1) % PKT_QUEUE;
    return (int)p->len;
}

static void rtl8139_rx(void) {
    /* CR bit 0 = BUFE: buffer empty */
    while (!(inb(io_base + REG_CMD) & 0x01)) {
        uint32_t off = rx_off % 8192;
        uint16_t *hdr = (uint16_t *)(rx_buf + off);
        uint16_t status = hdr[0];
        uint16_t len = hdr[1];
        if (status == 0 || len == 0) break;
        if (!(status & 0x01)) { /* ROK not set */
            break;
        }
        uint16_t pkt_len = len;
        if (pkt_len >= 4) pkt_len -= 4; /* strip CRC */
        if (pkt_len > 0 && pkt_len <= PKT_MAX) {
            uint8_t *pkt = rx_buf + off + 4;
            /* handle wrap-around */
            if (off + 4 + pkt_len <= RX_BUF_ALLOC) {
                rxq_push(pkt, pkt_len);
            } else {
                uint8_t tmp[PKT_MAX];
                uint32_t first = RX_BUF_ALLOC - (off + 4);
                if (first > pkt_len) first = pkt_len;
                memcpy(tmp, pkt, first);
                memcpy(tmp + first, rx_buf, pkt_len - first);
                rxq_push(tmp, pkt_len);
            }
        }
        rx_off = (rx_off + len + 4 + 3) & ~3u;
        outw(io_base + REG_CAPR, (uint16_t)(rx_off - 16));
    }
}

static void rtl8139_irq(void) {
    if (!present) return;
    uint16_t isr = inw(io_base + REG_ISR);
    if (!isr) return;
    outw(io_base + REG_ISR, isr); /* ack */
    if (isr & 0x01) rtl8139_rx(); /* ROK */
    if (isr & 0x10) rtl8139_rx(); /* Rx overflow - still drain */
}

void rtl8139_init(void) {
    pci_dev_t dev;
    present = 0;
    if (!pci_find(RTL_VENDOR, RTL_DEVICE, &dev)) {
        kprintf("[net] RTL8139 not found\n");
        return;
    }
    pci_enable_busmaster(&dev);
    io_base = dev.bar[0] & ~0x3u;
    if (!io_base) { kprintf("[net] RTL8139 bad BAR0\n"); return; }

    /* power on */
    outb(io_base + REG_CONFIG1, 0x00);
    /* software reset */
    outb(io_base + REG_CMD, 0x10);
    for (int i = 0; i < 100000; i++) {
        if (!(inb(io_base + REG_CMD) & 0x10)) break;
    }

    uint32_t rxp = alloc_contig_pages(3); /* 12K */
    if (!rxp) { kprintf("[net] RTL8139 rx alloc failed\n"); return; }
    rx_phys = rxp;
    rx_buf = (uint8_t *)phys_to_virt(rxp);
    memset(rx_buf, 0, RX_BUF_ALLOC);
    rx_off = 0;

    for (int i = 0; i < 4; i++) {
        uint32_t tp = pmm_alloc_frame();
        if (!tp) { kprintf("[net] RTL8139 tx alloc failed\n"); return; }
        tx_phys[i] = tp;
        tx_buf[i] = (uint8_t *)phys_to_virt(tp);
        memset(tx_buf[i], 0, PAGE_SIZE);
    }
    tx_cur = 0;

    outl(io_base + REG_RBSTART, rx_phys);
    outw(io_base + REG_CAPR, 0xFFF0);
    /* IMR: ROK + TOK + RER + TER */
    outw(io_base + REG_IMR, 0x0005);
    /* RCR: AB + AM + APM + AAP (promisc for simplicity) + WRAP, 8K buffer */
    outl(io_base + REG_RCR, 0x0000000F | (1u << 7));
    /* CR: RE + TE */
    outb(io_base + REG_CMD, 0x0C);

    for (int i = 0; i < 6; i++) mac[i] = inb(io_base + REG_MAC0 + i);

    irq_register(dev.irq_line, rtl8139_irq);
    present = 1;
    kprintf("[ok] RTL8139 io=%x irq=%u mac=%x:%x:%x:%x:%x:%x\n",
            io_base, dev.irq_line,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int rtl8139_present(void) { return present; }

void rtl8139_get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

int rtl8139_send(const void *data, uint32_t len) {
    if (!present || !data || len == 0) return -1;
    if (len > TX_BUF_SIZE) len = TX_BUF_SIZE;
    if (len < 60) { memset(tx_buf[tx_cur], 0, 60); } /* min ethernet frame */
    memcpy(tx_buf[tx_cur], data, len);
    uint32_t send_len = len < 60 ? 60 : len;
    uint32_t tsd = (uint32_t)(REG_TSD0 + tx_cur * 4);
    uint32_t tsad = (uint32_t)(REG_TSAD0 + tx_cur * 4);
    outl(io_base + tsad, tx_phys[tx_cur]);
    outl(io_base + tsd, send_len); /* clears OWN bit, starts send */
    tx_cur = (tx_cur + 1) % 4;
    return (int)send_len;
}

int rtl8139_recv(void *buf, uint32_t maxlen) {
    if (!present) return 0;
    uint32_t flags = read_eflags();
    cpu_cli();
    int r = rxq_pop(buf, maxlen);
    if (flags & 0x200) cpu_sti();
    return r;
}
