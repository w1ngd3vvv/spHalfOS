/* spHalfOS Internet Explorer-style browser - minimal HTTP/HTTPS client */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <syscall.h>
#include <fb.h>
#include <ui_font.h>

/* ===================== config ===================== */
#define WIN_W 900
#define WIN_H 620
#define TOOLBAR_H 52
#define STATUS_H 22
#define CONTENT_Y (TOOLBAR_H)
#define CONTENT_H (WIN_H - TOOLBAR_H - STATUS_H)
#define MAX_HTML (256 * 1024)
#define MAX_TEXT (128 * 1024)
#define MAX_LINKS 256
#define MAX_LINES 4096

/* ===================== GUI helpers ===================== */
static uint32_t fb[WIN_W * WIN_H];

#define C_FACE   0xC0C0C0u
#define C_LIGHT  0xFFFFFFu
#define C_SHADOW 0x808080u
#define C_DARK   0x000000u
#define C_NAVY   0x000080u
#define C_TEXT   0x000000u
#define C_WHITE  0xFFFFFFu
#define C_BLUE   0x0000FFu
#define C_VISIT  0x800080u
#define C_BG     0xFFFFFFu

static inline void px(int x, int y, uint32_t c) {
    if ((unsigned)x < WIN_W && (unsigned)y < WIN_H) fb[y * WIN_W + x] = c;
}
static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIN_W) w = WIN_W - x;
    if (y + h > WIN_H) h = WIN_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *r = fb + yy * WIN_W + x;
        for (int i = 0; i < w; i++) r[i] = c;
    }
}
static void edge(int x, int y, int w, int h, int raised) {
    uint32_t tl = raised ? C_LIGHT : C_SHADOW;
    uint32_t br = raised ? C_DARK : C_LIGHT;
    fill_rect(x, y, w, 1, tl);
    fill_rect(x, y, 1, h, tl);
    fill_rect(x, y + h - 1, w, 1, br);
    fill_rect(x + w - 1, y, 1, h, br);
}
static void edge3(int x, int y, int w, int h, int sunken) {
    uint32_t o1 = sunken ? C_SHADOW : C_LIGHT;
    uint32_t o2 = sunken ? C_DARK : C_FACE;
    uint32_t i1 = sunken ? C_LIGHT : C_SHADOW;
    uint32_t i2 = sunken ? C_FACE : C_DARK;
    fill_rect(x, y, w, 1, o1); fill_rect(x, y, 1, h, o1);
    fill_rect(x, y+h-1, w, 1, i1); fill_rect(x+w-1, y, 1, h, i1);
    fill_rect(x+1, y+1, w-2, 1, o2); fill_rect(x+1, y+1, 1, h-2, o2);
    fill_rect(x+1, y+h-2, w-2, 1, i2); fill_rect(x+w-2, y+1, 1, h-2, i2);
}

/* font from ui_font.h (Terminus 8x16) */
static void draw_char(int x, int y, char ch, uint32_t fg, int scale) {
    ui_draw_char(fb, WIN_W, WIN_H, x, y, ch, fg, scale);
}
static void draw_text(int x, int y, const char *s, uint32_t fg, int scale) {
    ui_draw_text(fb, WIN_W, WIN_H, x, y, s, fg, scale);
}
static void draw_text_n(int x, int y, const char *s, int n, uint32_t fg, int scale) {
    ui_draw_text_n(fb, WIN_W, WIN_H, x, y, s, n, fg, scale);
}

/* ===================== tiny network stack ===================== */
static uint8_t my_mac[6];
static uint8_t my_ip[4]  = {10,0,2,15};
static uint8_t gw_ip[4]  = {10,0,2,2};
static uint8_t dns_ip[4] = {10,0,2,3};
static uint8_t netmask[4]= {255,255,255,0};
static int net_ready;

static uint16_t htons16(uint16_t x){ return (uint16_t)((x<<8)|(x>>8)); }
static uint32_t htonl32(uint32_t x){ return ((x&0xFF)<<24)|((x&0xFF00)<<8)|((x&0xFF0000)>>8)|((x>>24)&0xFF); }
static uint16_t rd16(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t rd32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void wr16(uint8_t *p,uint16_t v){ p[0]=v>>8; p[1]=v&0xFF; }
static void wr32(uint8_t *p,uint32_t v){ p[0]=v>>24; p[1]=(v>>16)&0xFF; p[2]=(v>>8)&0xFF; p[3]=v&0xFF; }
static int ip_eq(const uint8_t *a,const uint8_t *b){ return memcmp(a,b,4)==0; }
static int ip_is_local(const uint8_t *ip){
    for(int i=0;i<4;i++) if ((ip[i]&netmask[i]) != (my_ip[i]&netmask[i])) return 0;
    return 1;
}

static uint16_t ip_checksum(const void *data, int len){
    const uint8_t *p=(const uint8_t*)data;
    uint32_t sum=0;
    while(len>1){ sum += (uint16_t)((p[0]<<8)|p[1]); p+=2; len-=2; }
    if(len) sum += (uint16_t)(p[0]<<8);
    while(sum>>16) sum = (sum&0xFFFF) + (sum>>16);
    return (uint16_t)(~sum);
}

/* ---- ARP ---- */
#define ARP_CACHE 16
typedef struct { uint8_t ip[4]; uint8_t mac[6]; int valid; } arp_ent_t;
static arp_ent_t arp_cache[ARP_CACHE];

static void arp_cache_put(const uint8_t *ip, const uint8_t *mac){
    for(int i=0;i<ARP_CACHE;i++) if(arp_cache[i].valid && ip_eq(arp_cache[i].ip,ip)){ memcpy(arp_cache[i].mac,mac,6); return; }
    for(int i=0;i<ARP_CACHE;i++) if(!arp_cache[i].valid){ arp_cache[i].valid=1; memcpy(arp_cache[i].ip,ip,4); memcpy(arp_cache[i].mac,mac,6); return; }
    memcpy(arp_cache[0].ip,ip,4); memcpy(arp_cache[0].mac,mac,6); arp_cache[0].valid=1;
}
static const uint8_t *arp_cache_get(const uint8_t *ip){
    for(int i=0;i<ARP_CACHE;i++) if(arp_cache[i].valid && ip_eq(arp_cache[i].ip,ip)) return arp_cache[i].mac;
    return NULL;
}
static void arp_send_request(const uint8_t *target_ip);
static void net_poll(void);
static void net_sleep_ms(int ms);
static int arp_resolve(const uint8_t *ip, uint8_t mac_out[6], int timeout_ms){
    const uint8_t *next = ip_is_local(ip) ? ip : gw_ip;
    const uint8_t *m = arp_cache_get(next);
    if(m){ if(mac_out) memcpy(mac_out,m,6); return 0; }
    for(int waited=0; waited<timeout_ms; waited+=100){
        arp_send_request(next);
        for(int i=0;i<5;i++){ net_poll(); net_sleep_ms(20); waited+=20; if(waited>=timeout_ms) break; }
        m = arp_cache_get(next);
        if(m){ if(mac_out) memcpy(mac_out,m,6); return 0; }
    }
    return -1;
}

static void eth_send(const uint8_t *dst_mac, uint16_t ethertype, const void *payload, int plen){
    uint8_t frame[1600];
    if (plen > 1500) plen = 1500;
    memcpy(frame, dst_mac, 6);
    memcpy(frame+6, my_mac, 6);
    wr16(frame+12, ethertype);
    memcpy(frame+14, payload, plen);
    int flen = 14 + plen;
    if (flen < 60) { memset(frame+flen, 0, 60-flen); flen=60; }
    sp_net_send(frame, flen);
}

static void arp_send_request(const uint8_t *target_ip){
    uint8_t p[28];
    wr16(p+0,1); wr16(p+2,0x0800); p[4]=6; p[5]=4; wr16(p+6,1);
    memcpy(p+8,my_mac,6); memcpy(p+14,my_ip,4);
    memset(p+18,0,6); memcpy(p+24,target_ip,4);
    uint8_t bcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    eth_send(bcast, 0x0806, p, 28);
}

static void arp_handle(const uint8_t *p, int len){
    if(len < 28) return;
    uint16_t op = rd16(p+6);
    const uint8_t *sha=p+8, *spa=p+14, *tha=p+18, *tpa=p+24;
    arp_cache_put(spa, sha);
    if(op==1 && ip_eq(tpa,my_ip)){
        uint8_t rep[28];
        wr16(rep+0,1); wr16(rep+2,0x0800); rep[4]=6; rep[5]=4; wr16(rep+6,2);
        memcpy(rep+8,my_mac,6); memcpy(rep+14,my_ip,4);
        memcpy(rep+18,sha,6); memcpy(rep+24,spa,4);
        eth_send(sha,0x0806,rep,28);
    }
}

/* ---- IP/UDP/TCP common ---- */
static uint16_t ip_id_counter = 1;

static void ip_send(const uint8_t *dst_ip, uint8_t proto, const void *payload, int plen){
    const uint8_t *next_hop = ip_is_local(dst_ip) ? dst_ip : gw_ip;
    const uint8_t *dst_mac = arp_cache_get(next_hop);
    if(!dst_mac){
        arp_send_request(next_hop);
        /* caller should retry after polling */
        return;
    }
    uint8_t pkt[1500];
    if (plen > 1480) plen = 1480;
    uint8_t *ip = pkt;
    ip[0]=0x45; ip[1]=0; wr16(ip+2, (uint16_t)(20+plen)); wr16(ip+4, ip_id_counter++); wr16(ip+6,0);
    ip[8]=64; ip[9]=proto; wr16(ip+10,0); memcpy(ip+12,my_ip,4); memcpy(ip+16,dst_ip,4);
    wr16(ip+10, ip_checksum(ip,20));
    memcpy(ip+20,payload,plen);
    eth_send(dst_mac, 0x0800, pkt, 20+plen);
}

/* ---- UDP ---- */
typedef void (*udp_cb_t)(const uint8_t *src_ip,uint16_t src_port,const uint8_t *data,int len);
static struct { int used; uint16_t port; udp_cb_t cb; } udp_bind[8];

static int udp_bind_port(uint16_t port, udp_cb_t cb){
    for(int i=0;i<8;i++) if(!udp_bind[i].used){ udp_bind[i].used=1; udp_bind[i].port=port; udp_bind[i].cb=cb; return i; }
    return -1;
}
static void udp_unbind(int h){ if(h>=0&&h<8) udp_bind[h].used=0; }
static void udp_send(const uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port, const void *data, int len){
    uint8_t buf[1400];
    if(len>1380) len=1380;
    wr16(buf+0,src_port); wr16(buf+2,dst_port); wr16(buf+4,(uint16_t)(8+len)); wr16(buf+6,0);
    memcpy(buf+8,data,len);
    /* UDP checksum optional for IPv4; set 0 */
    ip_send(dst_ip,17,buf,8+len);
}
static void udp_handle(const uint8_t *src_ip, const uint8_t *p, int len){
    if(len < 8) return;
    uint16_t sport=rd16(p+0), dport=rd16(p+2), ulen=rd16(p+4);
    if(ulen < 8 || ulen > len) ulen=len;
    for(int i=0;i<8;i++) if(udp_bind[i].used && udp_bind[i].port==dport && udp_bind[i].cb)
        udp_bind[i].cb(src_ip,sport,p+8,ulen-8);
}

/* ---- TCP (single connection) ---- */
enum { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_FIN_WAIT, TCP_CLOSE_WAIT, TCP_LAST_ACK };
typedef struct {
    int state;
    uint8_t rip[4];
    uint16_t rport, lport;
    uint32_t snd_una, snd_nxt, rcv_nxt;
    uint8_t rbuf[32768];
    int rhead, rtail;
    int fin_received;
} tcp_conn_t;
static tcp_conn_t tc;

static uint16_t tcp_checksum(const uint8_t *src_ip, const uint8_t *dst_ip, const uint8_t *tcp, int len){
    uint32_t sum=0;
    const uint8_t ph[12] = {src_ip[0],src_ip[1],src_ip[2],src_ip[3], dst_ip[0],dst_ip[1],dst_ip[2],dst_ip[3], 0,6, (uint8_t)(len>>8),(uint8_t)(len&0xFF)};
    const uint8_t *p=ph; int l=12;
    while(l>1){ sum += (uint16_t)((p[0]<<8)|p[1]); p+=2; l-=2; }
    p=tcp; l=len;
    while(l>1){ sum += (uint16_t)((p[0]<<8)|p[1]); p+=2; l-=2; }
    if(l) sum += (uint16_t)(p[0]<<8);
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (uint16_t)(~sum);
}

static void tcp_send_seg(uint8_t flags, const void *data, int len){
    uint8_t seg[1500];
    if(len>1400) len=1400;
    uint8_t *t=seg;
    wr16(t+0, tc.lport); wr16(t+2, tc.rport);
    wr32(t+4, tc.snd_nxt); wr32(t+8, tc.rcv_nxt);
    t[12]=5<<4; t[13]=flags; wr16(t+14,4096); wr16(t+16,0); wr16(t+18,0);
    if(len>0) memcpy(t+20,data,len);
    int slen=20+len;
    wr16(t+16, tcp_checksum(my_ip, tc.rip, t, slen));
    ip_send(tc.rip,6,t,slen);
    if(flags & (0x02|0x01)) tc.snd_nxt++; /* SYN/FIN consume 1 */
    if(len>0) tc.snd_nxt += (uint32_t)len;
}

static void tcp_rbuf_put(const uint8_t *d, int len){
    for(int i=0;i<len;i++){
        int next=(tc.rhead+1)% (int)sizeof(tc.rbuf);
        if(next==tc.rtail) break;
        tc.rbuf[tc.rhead]=d[i]; tc.rhead=next;
    }
}
static int tcp_rbuf_get(uint8_t *out, int max){
    int n=0;
    while(n<max && tc.rtail!=tc.rhead){ out[n++]=tc.rbuf[tc.rtail]; tc.rtail=(tc.rtail+1)% (int)sizeof(tc.rbuf); }
    return n;
}
static int tcp_rbuf_avail(void){ return (tc.rhead - tc.rtail + (int)sizeof(tc.rbuf)) % (int)sizeof(tc.rbuf); }

static void tcp_handle(const uint8_t *src_ip, const uint8_t *p, int len){
    if(len < 20) return;
    uint16_t sport=rd16(p+0), dport=rd16(p+2);
    uint32_t seq=rd32(p+4), ack=rd32(p+8);
    uint8_t off=(p[12]>>4)*4; uint8_t flags=p[13];
    if(off < 20 || off > len) return;
    if(tc.state==TCP_CLOSED) return;
    if(dport!=tc.lport || sport!=tc.rport || !ip_eq(src_ip,tc.rip)) return;

    int payload = len - off;
    const uint8_t *data = p+off;

    if(flags & 0x04){ tc.state=TCP_CLOSED; return; } /* RST */
    if(tc.state==TCP_SYN_SENT){
        if((flags&0x12)==0x12){ /* SYN+ACK */
            tc.rcv_nxt = seq+1;
            tc.snd_una = ack;
            tcp_send_seg(0x10, NULL, 0); /* ACK */
            tc.state = TCP_ESTABLISHED;
        }
        return;
    }
    if(tc.state==TCP_ESTABLISHED || tc.state==TCP_FIN_WAIT || tc.state==TCP_CLOSE_WAIT){
        if(payload>0 && seq==tc.rcv_nxt){
            tcp_rbuf_put(data,payload);
            tc.rcv_nxt += (uint32_t)payload;
            tcp_send_seg(0x10,NULL,0);
        }
        if(flags & 0x10){ /* ACK */
            tc.snd_una = ack;
            if(tc.state==TCP_LAST_ACK) { tc.state=TCP_CLOSED; return; }
        }
        if(flags & 0x01){ /* FIN */
            if(seq == tc.rcv_nxt) { tc.rcv_nxt++; }
            tc.fin_received=1;
            tcp_send_seg(0x10,NULL,0);
            if(tc.state==TCP_ESTABLISHED) tc.state=TCP_CLOSE_WAIT;
            else if(tc.state==TCP_FIN_WAIT) tc.state=TCP_CLOSED;
        }
    }
}

static void ip_handle(const uint8_t *p, int len){
    if(len < 20) return;
    uint8_t ver = p[0]>>4; uint8_t ihl=(p[0]&0xF)*4;
    if(ver!=4 || ihl<20 || ihl>len) return;
    uint16_t tot=rd16(p+2);
    if(tot>len) tot=len;
    uint8_t proto=p[9];
    const uint8_t *src=p+12, *dst=p+16;
    if(!ip_eq(dst,my_ip)) return;
    const uint8_t *pl=p+ihl; int plen=tot-ihl;
    if(proto==17) udp_handle(src,pl,plen);
    else if(proto==6) tcp_handle(src,pl,plen);
}

static void net_poll(void){
    uint8_t buf[1600];
    for(int i=0;i<32;i++){
        int n = sp_net_recv(buf, sizeof(buf));
        if(n <= 0) break;
        if(n < 14) continue;
        uint16_t et = rd16(buf+12);
        if(et==0x0806) arp_handle(buf+14, n-14);
        else if(et==0x0800) ip_handle(buf+14, n-14);
    }
}

static void net_sleep_ms(int ms){ usleep(ms*1000); }

static int net_init(void){
    sp_netinfo_t ni;
    if(sp_net_info(&ni) < 0 || !ni.present) return -1;
    memcpy(my_mac, ni.mac, 6);
    net_ready = 1;
    return 0;
}

/* ---- DNS ---- */
static volatile int dns_done;
static uint8_t dns_result[4];
static int dns_ok;
static uint16_t dns_txid;

static void dns_cb(const uint8_t *src_ip, uint16_t src_port, const uint8_t *data, int len){
    (void)src_ip; (void)src_port;
    if(len < 12) return;
    uint16_t id=rd16(data+0), flags=rd16(data+2), qd=rd16(data+4), an=rd16(data+6);
    if(id!=dns_txid || !(flags&0x8000)) return;
    int off=12;
    for(int i=0;i<qd && off<len;i++){
        while(off<len && data[off]) off += data[off]+1;
        off++; off+=4;
    }
    for(int i=0;i<an && off+12<=len;i++){
        /* name */
        if((data[off]&0xC0)==0xC0) off+=2;
        else { while(off<len && data[off]) off += data[off]+1; off++; }
        if(off+10>len) break;
        uint16_t type=rd16(data+off); uint16_t rdlen=rd16(data+off+8); off+=10;
        if(off+rdlen>len) break;
        if(type==1 && rdlen==4){
            memcpy(dns_result, data+off, 4);
            dns_ok=1; dns_done=1; return;
        }
        off+=rdlen;
    }
    dns_done=1;
}

static int dns_resolve(const char *host, uint8_t out_ip[4]){
    /* numeric IP? */
    unsigned a,b,c,d;
    if(sscanf(host, "%u.%u.%u.%u", &a,&b,&c,&d)==4 && a<256&&b<256&&c<256&&d<256){
        out_ip[0]=a; out_ip[1]=b; out_ip[2]=c; out_ip[3]=d; return 0;
    }
    dns_done=0; dns_ok=0; dns_txid = (uint16_t)(sp_ticks() ^ 0xBEEF);
    int h = udp_bind_port(53053, dns_cb);
    if(h<0) return -1;
    uint8_t q[512]; int off=0;
    wr16(q+off, dns_txid); off+=2;
    wr16(q+off, 0x0100); off+=2; /* RD */
    wr16(q+off, 1); off+=2; wr16(q+off,0); off+=2; wr16(q+off,0); off+=2; wr16(q+off,0); off+=2;
    /* qname */
    const char *s=host;
    while(*s){
        const char *dot = strchr(s,'.');
        int lab = dot ? (int)(dot-s) : (int)strlen(s);
        if(lab<=0||lab>63){ udp_unbind(h); return -1; }
        q[off++]=(uint8_t)lab;
        memcpy(q+off,s,lab); off+=lab;
        s += lab;
        if(*s=='.') s++;
    }
    q[off++]=0;
    wr16(q+off,1); off+=2; /* A */
    wr16(q+off,1); off+=2; /* IN */
    if(arp_resolve(dns_ip, NULL, 2000) < 0){ udp_unbind(h); return -1; }
    for(int tries=0; tries<4 && !dns_done; tries++){
        udp_send(dns_ip, 53053, 53, q, off);
        for(int i=0;i<75 && !dns_done;i++){ net_poll(); net_sleep_ms(20); }
    }
    udp_unbind(h);
    if(dns_ok){ memcpy(out_ip,dns_result,4); return 0; }
    return -1;
}

/* ---- TCP helpers ---- */
static uint16_t next_lport = 40000;

static int tcp_connect(const uint8_t *ip, uint16_t port, int timeout_ms){
    if(arp_resolve(ip, NULL, 2000) < 0) return -1;
    memset(&tc,0,sizeof(tc));
    memcpy(tc.rip,ip,4); tc.rport=port; tc.lport=next_lport++;
    if(next_lport>60000) next_lport=40000;
    tc.snd_nxt = (uint32_t)sp_ticks()*12345u + 1;
    tc.snd_una = tc.snd_nxt;
    tc.state = TCP_SYN_SENT;
    tcp_send_seg(0x02,NULL,0); /* SYN */
    int waited=0;
    while(waited < timeout_ms){
        net_poll();
        if(tc.state==TCP_ESTABLISHED) return 0;
        if(tc.state==TCP_CLOSED) { /* retry SYN once in a while */ }
        net_sleep_ms(20); waited+=20;
        if(tc.state==TCP_SYN_SENT && (waited%500)==0) tcp_send_seg(0x02,NULL,0);
    }
    tc.state=TCP_CLOSED;
    return -1;
}

static int tcp_send_all(const void *data, int len, int timeout_ms){
    const uint8_t *p=(const uint8_t*)data;
    int sent=0, waited=0;
    while(sent < len && waited < timeout_ms){
        net_poll();
        if(tc.state!=TCP_ESTABLISHED && tc.state!=TCP_CLOSE_WAIT) return -1;
        int chunk = len - sent;
        if(chunk > 1024) chunk = 1024;
        /* wait until previous data acked roughly */
        if((int)(tc.snd_nxt - tc.snd_una) > 4096){ net_sleep_ms(20); waited+=20; continue; }
        uint32_t old_nxt = tc.snd_nxt;
        tcp_send_seg(0x10|0x08, p+sent, chunk); /* PSH+ACK */
        if(tc.snd_nxt > old_nxt) sent += (int)(tc.snd_nxt - old_nxt);
        waited=0;
    }
    return sent==len ? 0 : -1;
}

static int tcp_recv_some(uint8_t *buf, int max, int timeout_ms){
    int waited=0;
    while(waited < timeout_ms){
        net_poll();
        int n = tcp_rbuf_get(buf,max);
        if(n>0) return n;
        if(tc.state==TCP_CLOSED || tc.state==TCP_CLOSE_WAIT) return 0;
        net_sleep_ms(20); waited+=20;
    }
    return 0;
}

static void tcp_close_conn(void){
    if(tc.state==TCP_ESTABLISHED){
        tcp_send_seg(0x11,NULL,0); /* FIN+ACK */
        tc.state=TCP_FIN_WAIT;
        for(int i=0;i<100;i++){ net_poll(); if(tc.state==TCP_CLOSED) break; net_sleep_ms(20); }
    } else if(tc.state==TCP_CLOSE_WAIT){
        tcp_send_seg(0x11,NULL,0);
        tc.state=TCP_LAST_ACK;
        for(int i=0;i<50;i++){ net_poll(); if(tc.state==TCP_CLOSED) break; net_sleep_ms(20); }
    }
    tc.state=TCP_CLOSED;
}

/* ===================== HTTP ===================== */
typedef struct { char scheme[8]; char host[128]; int port; char path[512]; } url_t;

static int parse_url(const char *u, url_t *out){
    memset(out,0,sizeof(*out));
    strcpy(out->scheme,"http"); out->port=80; strcpy(out->path,"/");
    const char *p=u;
    const char *sc=strstr(p,"://");
    if(sc){
        int sl=sc-p; if(sl>7) sl=7;
        memcpy(out->scheme,p,sl); out->scheme[sl]=0;
        p=sc+3;
    }
    if(strcmp(out->scheme,"http")==0) out->port=80;
    else if(strcmp(out->scheme,"https")==0) out->port=443;
    const char *slash=strchr(p,'/');
    const char *hostend=slash?slash:p+strlen(p);
    char hostport[160]; int hl=hostend-p; if(hl>159) hl=159;
    memcpy(hostport,p,hl); hostport[hl]=0;
    char *colon=strchr(hostport,':');
    if(colon){ *colon=0; out->port=atoi(colon+1); }
    strncpy(out->host,hostport,sizeof(out->host)-1);
    if(slash) strncpy(out->path,slash,sizeof(out->path)-1);
    return out->host[0]?0:-1;
}

static int http_fetch(const char *url, char *body, int maxbody, char *status, int status_sz, char *redir, int redir_sz){
    if(redir && redir_sz) redir[0]=0;
    if(status && status_sz) snprintf(status,status_sz,"Resolving...");
    url_t u;
    if(parse_url(url,&u)<0){ snprintf(status,status_sz,"Bad URL"); return -1; }
    if(strcmp(u.scheme,"https")==0){
        snprintf(status,status_sz,"HTTPS not supported yet (TLS required)");
        return -1;
    }
    uint8_t ip[4];
    if(dns_resolve(u.host,ip)<0){ snprintf(status,status_sz,"DNS failed: %s",u.host); return -1; }
    snprintf(status,status_sz,"Connecting %d.%d.%d.%d:%d...",ip[0],ip[1],ip[2],ip[3],u.port);
    if(tcp_connect(ip,(uint16_t)u.port,5000)<0){ snprintf(status,status_sz,"Connect failed"); return -1; }

    char req[1024];
    snprintf(req,sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: spHalfOS-IE/0.1\r\n"
        "Accept: text/html,text/plain,*/*\r\n"
        "Connection: close\r\n\r\n", u.path, u.host);
    if(tcp_send_all(req,(int)strlen(req),5000)<0){ tcp_close_conn(); snprintf(status,status_sz,"Send failed"); return -1; }
    snprintf(status,status_sz,"Receiving...");

    char *resp = (char*)malloc(MAX_HTML);
    if(!resp){ tcp_close_conn(); snprintf(status,status_sz,"Out of memory"); return -1; }
    int rlen=0;
    while(rlen < MAX_HTML-1){
        int n = tcp_recv_some((uint8_t*)resp+rlen, MAX_HTML-1-rlen, 5000);
        if(n<=0) break;
        rlen+=n;
        snprintf(status,status_sz,"Receiving... %d bytes", rlen);
    }
    resp[rlen]=0;
    tcp_close_conn();

    /* parse status */
    int code=0;
    if(rlen>12 && strncmp(resp,"HTTP/",5)==0){ code = atoi(resp+9); }
    char *hdr_end = strstr(resp,"\r\n\r\n");
    int hdr_len = hdr_end ? (int)(hdr_end - resp) : 0;
    char *bodyp = hdr_end ? hdr_end+4 : resp;
    int bodylen = rlen - (int)(bodyp - resp);

    /* redirect */
    if(code>=300 && code<400 && hdr_end){
        char hdrs[4096]; int hcopy = hdr_len < 4095 ? hdr_len : 4095;
        memcpy(hdrs, resp, hcopy); hdrs[hcopy]=0;
        for(char *ln=hdrs; *ln; ){
            char *eol=strstr(ln,"\r\n"); if(eol) *eol=0;
            if(strncasecmp(ln,"Location:",9)==0){
                char *loc=ln+9; while(*loc==' ') loc++;
                if(redir && redir_sz) strncpy(redir,loc,redir_sz-1);
            }
            if(!eol) break;
            ln=eol+2;
        }
    }

    if(bodylen > maxbody-1) bodylen = maxbody-1;
    memcpy(body, bodyp, bodylen);
    body[bodylen]=0;
    snprintf(status,status_sz,"HTTP %d (%d bytes)", code, bodylen);
    free(resp);
    return code;
}

/* ===================== simple HTML -> text ===================== */
typedef struct { int x,y,w,h; char url[256]; } link_t;
typedef struct { char text[256]; int len; int is_heading; int is_link; int link_idx; } line_t;
static line_t lines[MAX_LINES];
static int line_count;
static link_t links[MAX_LINKS];
static int link_count;
static char page_title[128];

static void add_line(const char *s, int len, int heading, int link_idx){
    if(line_count >= MAX_LINES) return;
    while(len>0 && (s[len-1]==' '||s[len-1]=='\t')) len--;
    line_t *l=&lines[line_count++];
    if(len>255) len=255;
    memcpy(l->text,s,len); l->text[len]=0; l->len=len;
    l->is_heading=heading; l->is_link = (link_idx>=0); l->link_idx=link_idx;
}
static int html_entity(const char *p, char *out){
    if(strncmp(p,"&amp;",5)==0){*out='&';return 5;}
    if(strncmp(p,"&lt;",4)==0){*out='<';return 4;}
    if(strncmp(p,"&gt;",4)==0){*out='>';return 4;}
    if(strncmp(p,"&quot;",6)==0){*out='"';return 6;}
    if(strncmp(p,"&nbsp;",6)==0){*out=' ';return 6;}
    if(strncmp(p,"&#39;",5)==0){*out='\'';return 5;}
    return 0;
}
static void html_to_text(const char *html){
    line_count=0; link_count=0; page_title[0]=0;
    char cur[256]; int clen=0; int in_tag=0; char tag[64]; int tlen=0;
    int in_script=0,in_style=0,in_title=0,in_a=0;
    char a_href[256]; a_href[0]=0; int cur_link=-1; int heading=0;
    int last_space=1;
    const char *p=html;
    while(*p){
        if(in_tag){
            if(*p=='>'){
                tag[tlen]=0; in_tag=0;
                /* process tag */
                char tl[64]; int i=0; for(;tag[i] && i<63;i++) tl[i]=(char)tolower((unsigned char)tag[i]); tl[i]=0;
                int closing = (tl[0]=='/');
                char *tn = closing?tl+1:tl;
                while(*tn==' ') tn++;
                char tname[32]; int j=0; while(tn[j] && tn[j]!=' ' && tn[j]!='/') { tname[j]=tn[j]; j++; } tname[j]=0;
                if(strcmp(tname,"script")==0){ in_script=!closing; }
                else if(strcmp(tname,"style")==0){ in_style=!closing; }
                else if(strcmp(tname,"title")==0){ in_title=!closing; if(closing){ /* title captured in cur handled? */ } }
                else if(strcmp(tname,"br")==0 || strcmp(tname,"p")==0 || strcmp(tname,"div")==0 ||
                        strcmp(tname,"li")==0 || strcmp(tname,"tr")==0 || strcmp(tname,"h1")==0 ||
                        strcmp(tname,"h2")==0 || strcmp(tname,"h3")==0 || strcmp(tname,"h4")==0 ||
                        strcmp(tname,"h5")==0 || strcmp(tname,"h6")==0){
                    if(clen>0){ add_line(cur,clen,heading,cur_link); clen=0; cur_link=-1; }
                    if(strcmp(tname,"li")==0){ cur[0]='*';cur[1]=' ';clen=2; }
                    if(tname[0]=='h' && tname[1]>='1' && tname[1]<='6' && !closing){ heading=1; }
                    if(tname[0]=='h' && closing){ heading=0; }
                    last_space=1;
                }
                else if(strcmp(tname,"a")==0){
                    if(!closing){
                        a_href[0]=0;
                        char *h=NULL; {
                            char *hay=tag; const char *nee="href=";
                            for(;*hay;hay++){ int k=0; while(nee[k] && hay[k] && tolower((unsigned char)hay[k])==tolower((unsigned char)nee[k])) k++; if(!nee[k]){h=hay;break;} }
                        }
                        if(h){
                            h+=5; while(*h==' ') h++;
                            char q=*h; if(q=='"'||q=='\''){ h++; char *e=strchr(h,q); int hl=e?(int)(e-h):0; if(hl>255) hl=255; memcpy(a_href,h,hl); a_href[hl]=0; }
                            else { int hl=0; while(h[hl] && h[hl]!=' ' && h[hl]!='>') hl++; if(hl>255) hl=255; memcpy(a_href,h,hl); a_href[hl]=0; }
                        }
                        if(a_href[0] && link_count < MAX_LINKS){
                            cur_link = link_count;
                            strncpy(links[link_count].url,a_href,255);
                            links[link_count].url[255]=0;
                            links[link_count].x=links[link_count].y=links[link_count].w=links[link_count].h=0;
                            link_count++;
                        }
                        in_a=1;
                    } else { in_a=0; cur_link=-1; }
                }
                p++; continue;
            } else {
                if(tlen < 63) tag[tlen++]=*p;
                p++; continue;
            }
        }
        if(*p=='<'){ in_tag=1; tlen=0; p++; continue; }
        if(in_script||in_style){ p++; continue; }
        char ch=*p++;
        if(ch=='\r'||ch=='\n'||ch=='\t') ch=' ';
        if(ch=='&'){
            char dec=0; int elen=html_entity(p-1,&dec);
            if(elen){ ch=dec; p += elen-1; }
        }
        if(in_title){
            int tl=strlen(page_title);
            if(tl < 127){ page_title[tl]=ch; page_title[tl+1]=0; }
            continue;
        }
        if(ch==' '){ if(last_space) continue; last_space=1; }
        else last_space=0;
        if(clen < 255){ cur[clen++]=ch; }
        else { add_line(cur,clen,heading,cur_link); clen=0; cur_link=-1; cur[clen++]=ch; }
    }
    if(clen>0) add_line(cur,clen,heading,cur_link);
}

/* ===================== Browser UI ===================== */
static char addr[512] = "http://example.com";
static char status[128] = "Ready";
static int scroll_y;
static int content_max_y;
static char body[MAX_TEXT];
static int loading;
static char history[8][512];
static int hist_count, hist_pos;

static void status_set(const char *s){ strncpy(status,s,sizeof(status)-1); status[sizeof(status)-1]=0; }

static void history_push(const char *url){
    if(hist_pos < hist_count-1) hist_count = hist_pos+1;
    if(hist_count < 8){ strncpy(history[hist_count],url,511); history[hist_count][511]=0; hist_count++; hist_pos=hist_count-1; }
    else { for(int i=1;i<8;i++) strcpy(history[i-1],history[i]); strncpy(history[7],url,511); history[7][511]=0; hist_pos=7; hist_count=8; }
}

static void render_content(void){
    fill_rect(0, CONTENT_Y, WIN_W, CONTENT_H, C_BG);
    int y = CONTENT_Y + 8 - scroll_y;
    int x0 = 16;
    int wrap = (WIN_W - 32) / 8; /* chars at scale 1 */
    for(int i=0;i<line_count;i++){
        line_t *l=&lines[i];
        if(y > WIN_H - STATUS_H) break;
        if(y + 18 < CONTENT_Y){ y += 18; continue; }
        uint32_t fg = l->is_heading ? C_NAVY : (l->is_link ? C_BLUE : C_TEXT);
        int scale = l->is_heading ? 2 : 2;
        int maxc = wrap;
        int start=0;
        while(start < l->len){
            int chunk = l->len - start;
            if(chunk > maxc) chunk = maxc;
            if(y >= CONTENT_Y-18 && y < WIN_H-STATUS_H){
                draw_text_n(x0, y, l->text+start, chunk, fg, scale);
                if(l->is_link){
                    int lw = 0; for(int ci2=start; ci2<start+chunk && l->text[ci2]; ci2++) lw += ui_char_width(l->text[ci2], scale);
                    fill_rect(x0, y+14, lw, 1, C_BLUE);
                    if(l->link_idx>=0 && l->link_idx<link_count){
                        links[l->link_idx].x=x0; links[l->link_idx].y=y; links[l->link_idx].w=lw; links[l->link_idx].h=16;
                    }
                }
            }
            y += 18;
            start += chunk;
        }
        if(l->len==0) y += 10;
    }
    content_max_y = y + scroll_y - CONTENT_Y;
}

static void draw_toolbar(void){
    fill_rect(0,0,WIN_W,TOOLBAR_H,C_FACE);
    fill_rect(0,TOOLBAR_H-2,WIN_W,1,C_SHADOW);
    fill_rect(0,TOOLBAR_H-1,WIN_W,1,C_LIGHT);
    /* IE-style title band */
    fill_rect(0,0,WIN_W,20, C_NAVY);
    draw_text(6,3,"Internet Explorer - spHalfOS", C_WHITE, 1);

    /* buttons */
    const char *btns[]={"Back","Home","Reload"};
    int bx=6;
    int bws[3]={44,44,52};
    for(int i=0;i<3;i++){
        fill_rect(bx, 24, bws[i], 22, C_FACE); edge(bx,24,bws[i],22,1);
        draw_text(bx+4, 28, btns[i], C_TEXT, 1);
        bx += bws[i]+4;
    }
    /* address label */
    draw_text(bx+4, 28, "Address:", C_TEXT, 1);
    int ax = bx + 4 + 8*8 + 4;
    int aw = WIN_W - ax - 64;
    fill_rect(ax, 24, aw, 22, C_WHITE); edge(ax,24,aw,22,0);
    /* address text clipped */
    int maxa = (aw-8)/8;
    char atmp[512]; strncpy(atmp, addr, sizeof(atmp)-1); atmp[sizeof(atmp)-1]=0;
    int alen=strlen(atmp); if(alen>maxa) { draw_text_n(ax+4,28, atmp+alen-maxa, maxa, C_TEXT, 1); }
    else draw_text(ax+4,28, atmp, C_TEXT, 1);
    /* Go */
    int gx=ax+aw+4;
    fill_rect(gx,24,52,22,C_FACE); edge(gx,24,52,22,1);
    draw_text(gx+10,28,"Go",C_TEXT, 1);
}

static void draw_status(void){
    int sy=WIN_H-STATUS_H;
    fill_rect(0,sy,WIN_W,STATUS_H,C_FACE);
    fill_rect(0,sy,WIN_W,1,C_SHADOW);
    draw_text(6, sy+4, status, C_TEXT, 1);
}

static void draw_all(void){
    fill_rect(0,0,WIN_W,WIN_H,C_FACE);
    draw_toolbar();
    render_content();
    draw_status();
}

static int point_in(int px,int py,int x,int y,int w,int h){ return px>=x&&px<x+w&&py>=y&&py<y+h; }

static void navigate(const char *url, int push_hist){
    char use[512]; strncpy(use,url,sizeof(use)-1); use[sizeof(use)-1]=0;
    /* trim */
    while(*use==' ') memmove(use,use+1,strlen(use));
    if(strncmp(use,"http://",7)!=0 && strncmp(use,"https://",8)!=0){
        char tmp[512]; snprintf(tmp,sizeof(tmp),"http://%s",use); strncpy(use,tmp,sizeof(use)-1);
    }
    strncpy(addr,use,sizeof(addr)-1); addr[sizeof(addr)-1]=0;
    loading=1;
    status_set("Loading...");
    draw_all(); sp_win_present(fb,WIN_W,WIN_H);

    char redir[512]; redir[0]=0;
    int code = http_fetch(addr, body, MAX_TEXT, status, sizeof(status), redir, sizeof(redir));
    if(code>=300 && code<400 && redir[0]){
        /* handle relative redirects */
        if(strncmp(redir,"http://",7)==0 || strncmp(redir,"https://",8)==0){
            strncpy(use,redir,sizeof(use)-1);
        } else if(redir[0]=='/'){
            url_t u; parse_url(addr,&u);
            snprintf(use,sizeof(use),"%s://%s%s%s",u.scheme,u.host, (u.port!=80&&u.port!=443)?"":"" , redir);
            if(u.port!=80 && u.port!=443){ snprintf(use,sizeof(use),"%s://%s:%d%s",u.scheme,u.host,u.port,redir); }
        } else {
            snprintf(use,sizeof(use),"%s",redir);
        }
        strncpy(addr,use,sizeof(addr)-1);
        status_set("Redirecting...");
        draw_all(); sp_win_present(fb,WIN_W,WIN_H);
        code = http_fetch(addr, body, MAX_TEXT, status, sizeof(status), NULL, 0);
    }
    if(code < 0 && body[0]==0){
        snprintf(body,sizeof(body),"<html><title>Error</title><body><h1>Cannot display page</h1><p>%s</p><p>URL: %s</p></body></html>", status, addr);
    }
    html_to_text(body);
    if(page_title[0]) { /* show in status */ }
    scroll_y=0;
    if(push_hist) history_push(addr);
    loading=0;
}

static void go_back(void){
    if(hist_pos > 0){ hist_pos--; strncpy(addr,history[hist_pos],sizeof(addr)-1); navigate(addr,0); }
}
static void go_home(void){ navigate("http://info.cern.ch/", 1); }

int main(void){
    memset(fb,0,sizeof(fb));
    body[0]=0;
    html_to_text("<html><title>spHalfOS IE</title><body><h1>Welcome to Internet Explorer</h1>"
                 "<p>spHalfOS browser with a minimal network stack.</p>"
                 "<p>Try: http://example.com or http://info.cern.ch</p>"
                 "<p>Wikipedia uses HTTPS; HTTPS/TLS is not implemented yet.</p></body></html>");

    if(net_init() < 0){
        status_set("No network device (RTL8139)");
    } else {
        status_set("Ready");
    }

    int editing_addr = 0;
    char edit_buf[512]; edit_buf[0]=0;

    sp_mouse_t m; int prev_buttons=0;
    if(mouse_read(&m)==0) prev_buttons=m.buttons;

    draw_all();
    sp_win_present(fb,WIN_W,WIN_H);

    for(;;){
        int need_draw=0;
        int c;
        while((c=sp_getkey()) >= 0){
            if(editing_addr){
                if(c=='\n' || c=='\r'){ editing_addr=0; strncpy(addr,edit_buf,sizeof(addr)-1); navigate(addr,1); need_draw=1; }
                else if(c==27){ editing_addr=0; need_draw=1; }
                else if(c==8 || c==127){ int l=strlen(edit_buf); if(l>0) edit_buf[l-1]=0; need_draw=1; }
                else if(c>=32 && c<127){ int l=strlen(edit_buf); if(l<(int)sizeof(edit_buf)-1){ edit_buf[l]=c; edit_buf[l+1]=0; } need_draw=1; }
            } else {
                if(c==27) return 0;
                else if(c=='\n' || c=='\r') { navigate(addr,1); need_draw=1; }
                else if(c=='b') { go_back(); need_draw=1; }
                else if(c=='h') { go_home(); need_draw=1; }
                else if(c=='r') { navigate(addr,0); need_draw=1; }
                else if(c=='g') { editing_addr=1; strncpy(edit_buf,addr,sizeof(edit_buf)-1); need_draw=1; }
                else if(c=='j' || c==0x1002){ scroll_y += 24; need_draw=1; }
                else if(c=='k' || c==0x1001){ scroll_y -= 24; if(scroll_y<0) scroll_y=0; need_draw=1; }
                else if(c==0x1006){ scroll_y += CONTENT_H-40; need_draw=1; } /* pgdn */
                else if(c==0x1005){ scroll_y -= CONTENT_H-40; if(scroll_y<0) scroll_y=0; need_draw=1; } /* pgup */
                else if(c==27){ /* handled */ }
            }
        }
        if(mouse_read(&m)==0){
            sp_wininfo_t wi;
            if(sp_win_info(&wi)==0){
                int mx=m.x-wi.x, my=m.y-wi.y;
                int left=m.buttons&1;
                int pressed = left && !(prev_buttons&1);
                if(pressed && mx>=0&&mx<WIN_W&&my>=0&&my<WIN_H){
                    if(point_in(mx,my,6,24,44,22)) { go_back(); need_draw=1; }
                    else if(point_in(mx,my,54,24,44,22)) { go_home(); need_draw=1; }
                    else if(point_in(mx,my,102,24,52,22)) { navigate(addr,0); need_draw=1; }
                    else if(point_in(mx,my,WIN_W-46,24,40,22)) { navigate(addr,1); need_draw=1; }
                    else if(my>=24 && my<46 && mx>170 && mx<WIN_W-50){ editing_addr=1; strncpy(edit_buf,addr,sizeof(edit_buf)-1); need_draw=1; }
                    else if(my>=CONTENT_Y && my<WIN_H-STATUS_H){
                        editing_addr=0;
                        for(int i=0;i<link_count;i++){
                            link_t *L=&links[i];
                            if(L->w>0 && point_in(mx,my,L->x,L->y,L->w,L->h)){
                                /* resolve relative */
                                char full[512];
                                if(strncmp(L->url,"http://",7)==0||strncmp(L->url,"https://",8)==0) strncpy(full,L->url,sizeof(full)-1);
                                else if(L->url[0]=='/'){ url_t u; parse_url(addr,&u); snprintf(full,sizeof(full),"%s://%s:%d%s",u.scheme,u.host,u.port,L->url); }
                                else { url_t u; parse_url(addr,&u); char base[512]; strncpy(base,u.path,sizeof(base)-1); char *sl=strrchr(base,'/'); if(sl) sl[1]=0; else strcpy(base,"/"); snprintf(full,sizeof(full),"%s://%s:%d%s%s",u.scheme,u.host,u.port,base,L->url); }
                                full[sizeof(full)-1]=0;
                                navigate(full,1); need_draw=1; break;
                            }
                        }
                    }
                }
                prev_buttons=m.buttons;
            }
        }

        if(editing_addr){
            /* show edit buffer in address bar */
            char saved[512]; strncpy(saved,addr,sizeof(saved)-1);
            snprintf(addr,sizeof(addr),"%s_", edit_buf);
            draw_all(); sp_win_present(fb,WIN_W,WIN_H);
            strncpy(addr,saved,sizeof(addr)-1);
            usleep(16000);
            continue;
        }

        if(scroll_y < 0) scroll_y = 0;
        if(need_draw){ draw_all(); sp_win_present(fb,WIN_W,WIN_H); }
        usleep(16000);
    }
}
