// =============================================================================
// captive_dns.cpp — tiny UDP:53 catch-all DNS for the setup AP.
//
// Answers every A query with the SoftAP gateway (192.168.4.1) so a phone that
// joins "Magic-8-Ball-Setup" resolves any hostname to the config portal and pops
// the "Sign in to network" sheet. Runs ONLY while the AP is up; in STA mode it
// must not run (it would hijack the device's own DNS).
// =============================================================================

#include "captive_dns.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "cdns";

// 192.168.4.1 in network byte order, as 4 RDATA bytes.
static const uint8_t AP_IP_BYTES[4] = {192, 168, 4, 1};

static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;

// DNS header is 12 bytes; flags live at offset 2..3.
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

// Build a response in `buf` (which already holds the `qlen`-byte query). Returns
// the response length, or 0 to drop. Strategy: echo the question, append one A
// answer pointing at the AP IP. Only answers a single-question A/ANY query.
static int build_reply(uint8_t *buf, int qlen, int cap)
{
    if (qlen < (int)sizeof(dns_header_t)) {
        return 0;
    }
    dns_header_t *h = (dns_header_t *)buf;
    // Must be a standard query (QR=0) with at least one question.
    if ((ntohs(h->flags) & 0x8000) != 0 || ntohs(h->qdcount) == 0) {
        return 0;
    }

    // Walk the first question's QNAME (label sequence ending in a 0 byte), then
    // skip QTYPE(2)+QCLASS(2) to find the end of the question section.
    int p = sizeof(dns_header_t);
    while (p < qlen && buf[p] != 0) {
        p += buf[p] + 1;        // jump over one label
        if (p >= qlen) {
            return 0;           // malformed
        }
    }
    p += 1;                     // the terminating 0 byte
    p += 4;                     // QTYPE + QCLASS
    if (p > qlen) {
        return 0;
    }
    int qend = p;

    // Answer record: name pointer 0xC00C -> the question at offset 12, type A(1),
    // class IN(1), TTL 60, RDLENGTH 4, RDATA = AP IP. 16 bytes total.
    if (qend + 16 > cap) {
        return 0;
    }
    h->flags   = htons(0x8180); // QR=1, RD=1 (assumed), RA=1, rcode=0
    h->ancount = htons(1);
    h->nscount = 0;
    h->arcount = 0;

    uint8_t *a = buf + qend;
    a[0] = 0xC0; a[1] = 0x0C;           // name: pointer to offset 12
    a[2] = 0x00; a[3] = 0x01;           // type A
    a[4] = 0x00; a[5] = 0x01;           // class IN
    a[6] = 0x00; a[7] = 0x00;
    a[8] = 0x00; a[9] = 0x3C;           // TTL 60s
    a[10] = 0x00; a[11] = 0x04;         // RDLENGTH 4
    a[12] = AP_IP_BYTES[0]; a[13] = AP_IP_BYTES[1];
    a[14] = AP_IP_BYTES[2]; a[15] = AP_IP_BYTES[3];

    return qend + 16;
}

static void cdns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ESP_LOGE(TAG, "bind(:53) failed");
        close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Don't block forever, so the task can notice s_running going false and exit.
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "captive DNS up on :53");
    uint8_t buf[512];   // classic DNS-over-UDP max
    while (s_running) {
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &sl);
        if (n <= 0) {
            continue;   // timeout or error -> re-check s_running
        }
        int rlen = build_reply(buf, n, sizeof(buf));
        if (rlen > 0) {
            sendto(sock, buf, rlen, 0, (struct sockaddr *)&src, sl);
        }
    }
    close(sock);
    ESP_LOGI(TAG, "captive DNS stopped");
    s_task = NULL;
    vTaskDelete(NULL);
}

int captive_dns_start(void)
{
    if (s_task) {
        return 0;   // already running
    }
    s_running = true;
    if (xTaskCreate(cdns_task, "cdns", 3072, NULL, 5, &s_task) != pdPASS) {
        s_running = false;
        return -1;
    }
    return 0;
}

int captive_dns_stop(void)
{
    s_running = false;   // the task closes its socket and self-deletes
    return 0;
}
