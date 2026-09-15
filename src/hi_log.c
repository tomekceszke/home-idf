#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

#include "hi_log.h"
#include "hi_wifi.h"

#define RAW_SIZE 256
#define OUT_SIZE 300            // datetime replacing the uptime digits may be longer
#define RINGBUF_SIZE 4096
#define EPOCH_2020 1577836800L

static int s_sock = -1;
static struct sockaddr_in s_addr;
static RingbufHandle_t s_ringbuf;
static void (*s_on_error_line)(const char *line);

/* "(12345)" -> "(HH:MM:SS)" once the wall clock is set; unchanged otherwise. */
static void replace_timestamp(const char *raw, char *out, size_t out_size)
{
    const char *open = strchr(raw, '(');
    const char *close = open ? strchr(open, ')') : NULL;
    time_t now = time(NULL);
    if (open == NULL || close == NULL || now < EPOCH_2020) {
        snprintf(out, out_size, "%s", raw);
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    char hms[10];
    strftime(hms, sizeof(hms), "%H:%M:%S", &t);
    snprintf(out, out_size, "%.*s(%s)%s", (int) (open - raw), raw, hms, close + 1);
}

static int log_vprintf(const char *fmt, va_list args)
{
    char raw[RAW_SIZE];
    int len = vsnprintf(raw, sizeof(raw), fmt, args);
    if (len <= 0) return len;

    char out[OUT_SIZE];
    replace_timestamp(raw, out, sizeof(out));
    fputs(out, stdout);

    // Never touch the network from the logging task: UDP sends go through a ring buffer (dropped when full)
    if (s_ringbuf != NULL && !xPortInIsrContext()) {
        xRingbufferSend(s_ringbuf, out, strlen(out), 0);
    }
    if (s_on_error_line != NULL && out[0] == 'E' && out[1] == ' ') {
        s_on_error_line(out);
    }
    return len;
}

static void udp_task(void *arg)
{
    // Sockets need the lwIP stack, which exists only once WiFi is started; lines logged before stay in the buffer
    while (!hi_wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        RingbufHandle_t rb = s_ringbuf;
        s_ringbuf = NULL;           // stop producers first
        vTaskDelay(pdMS_TO_TICKS(100));
        vRingbufferDelete(rb);
        vTaskDelete(NULL);
    }
    for (;;) {
        size_t size = 0;
        char *item = xRingbufferReceive(s_ringbuf, &size, portMAX_DELAY);
        if (item == NULL) continue;
        sendto(s_sock, item, size, 0, (struct sockaddr *) &s_addr, sizeof(s_addr));
        vRingbufferReturnItem(s_ringbuf, item);
    }
}

void hi_log_init(const hi_log_config_t *config)
{
    s_on_error_line = config->on_error_line;
    if (config->udp_ip != NULL && s_ringbuf == NULL) {
        memset(&s_addr, 0, sizeof(s_addr));
        s_addr.sin_family = AF_INET;
        s_addr.sin_port = htons(config->udp_port);
        if (inet_pton(AF_INET, config->udp_ip, &s_addr.sin_addr) == 1) {
            s_ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_NOSPLIT);
            if (s_ringbuf != NULL && xTaskCreate(udp_task, "hi_log", 3072, NULL, 2, NULL) != pdPASS) {
                vRingbufferDelete(s_ringbuf);
                s_ringbuf = NULL;
            }
        }
    }
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);
    esp_log_set_vprintf(log_vprintf);
}
