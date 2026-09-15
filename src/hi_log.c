#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

#include "hi_log.h"

#define RAW_SIZE 256
#define OUT_SIZE 300        // datetime replacing the uptime digits may be longer
#define EPOCH_2020 1577836800L

static int s_sock = -1;
static struct sockaddr_in s_addr;
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

    // Socket calls from the lwIP TCP/IP thread itself would wait on that same thread (deadlock)
    if (s_sock >= 0 && strcmp(pcTaskGetName(NULL), "tiT") != 0) {
        sendto(s_sock, out, strlen(out), 0, (struct sockaddr *) &s_addr, sizeof(s_addr));
    }
    if (s_on_error_line != NULL && out[0] == 'E' && out[1] == ' ') {
        s_on_error_line(out);
    }
    return len;
}

void hi_log_init(const hi_log_config_t *config)
{
    s_on_error_line = config->on_error_line;
    if (config->udp_ip != NULL && s_sock < 0) {
        memset(&s_addr, 0, sizeof(s_addr));
        s_addr.sin_family = AF_INET;
        s_addr.sin_port = htons(config->udp_port);
        if (inet_pton(AF_INET, config->udp_ip, &s_addr.sin_addr) == 1) {
            s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        }
    }
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);
    esp_log_set_vprintf(log_vprintf);
}
