/*
 * dd_dns.c — DNS cautivo mínimo: mientras el portal está abierto, cualquier
 * nombre resuelve a 192.168.4.1. Así el celular muestra solo la página de
 * configuración al conectarse a "DataDruida-XXXX".
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "dd_internal.h"

static const char *TAG = "dd_dns";
static TaskHandle_t s_task;
static volatile bool s_run;

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "No pude abrir el puerto 53");
        if (sock >= 0) close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
    }
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    while (s_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        if (buf[2] & 0x80) continue;                 // es una respuesta, no una consulta
        // Saltar la pregunta (QNAME + QTYPE + QCLASS)
        int p = 12;
        while (p < n && buf[p]) p += buf[p] + 1;
        p += 5;
        if (p > n) continue;
        uint16_t qtype = (buf[p - 4] << 8) | buf[p - 3];

        buf[2] = 0x84 | (buf[2] & 0x01);             // respuesta, autoritativa, copia RD
        buf[3] = 0x00;
        buf[6] = 0; buf[7] = (qtype == 1) ? 1 : 0;   // ANCOUNT: sólo para tipo A
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = p;
        if (qtype == 1) {
            const uint8_t ans[16] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1 };
            memcpy(buf + p, ans, sizeof(ans));
            len += sizeof(ans);
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&from, fl);
    }
    close(sock);
    s_task = NULL;
    vTaskDelete(NULL);
}

void dd_dns_start(void)
{
    if (s_task) return;
    s_run = true;
    xTaskCreate(dns_task, "dd_dns", 3072, NULL, 4, &s_task);
}

void dd_dns_stop(void) { s_run = false; }
