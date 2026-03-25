#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_littlefs.h"

#include "gadget_log.h"

/* --------------------------------------------------------------------------
 * Kconfig-derived write-level threshold
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_GADGET_LOG_WRITE_LEVEL_ERROR)
  #define GADGET_LOG_WRITE_LEVEL_INT  1
#elif defined(CONFIG_GADGET_LOG_WRITE_LEVEL_WARN)
  #define GADGET_LOG_WRITE_LEVEL_INT  2
#elif defined(CONFIG_GADGET_LOG_WRITE_LEVEL_INFO)
  #define GADGET_LOG_WRITE_LEVEL_INT  3
#elif defined(CONFIG_GADGET_LOG_WRITE_LEVEL_DEBUG)
  #define GADGET_LOG_WRITE_LEVEL_INT  4
#else
  #define GADGET_LOG_WRITE_LEVEL_INT  5
#endif

static const char *TAG = "gadget_log";

/* --------------------------------------------------------------------------
 * Static state
 * -------------------------------------------------------------------------- */
static char              s_ring_buf[CONFIG_GADGET_LOG_RING_BUF_SIZE];
static uint32_t          s_ring_head  = 0;   /* next write position */
static uint32_t          s_ring_tail  = 0;   /* next read position  */
static portMUX_TYPE      s_ring_mux   = portMUX_INITIALIZER_UNLOCKED;

static SemaphoreHandle_t s_file_mutex       = NULL;
static esp_timer_handle_t s_flush_timer     = NULL;
static uint16_t          s_write_idx        = 0;
static size_t            s_current_file_size = 0;
static bool              s_lfs_mounted      = false;
static volatile bool     s_time_synced      = false;

static vprintf_like_t    s_prev_vprintf     = NULL;

/* --------------------------------------------------------------------------
 * Ring-buffer helpers  (call with s_ring_mux held)
 * -------------------------------------------------------------------------- */

/* Write len bytes from src into the ring buffer; overwrites oldest on full. */
static void ring_buf_write(const char *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t next_head = (s_ring_head + 1) % CONFIG_GADGET_LOG_RING_BUF_SIZE;
        if (next_head == s_ring_tail) {
            /* Buffer full — advance tail to drop the oldest byte */
            s_ring_tail = (s_ring_tail + 1) % CONFIG_GADGET_LOG_RING_BUF_SIZE;
        }
        s_ring_buf[s_ring_head] = src[i];
        s_ring_head = next_head;
    }
}

/* Drain up to max bytes from the ring buffer into dest.  Returns byte count. */
static size_t ring_buf_drain(char *dest, size_t max)
{
    size_t count = 0;
    while (s_ring_tail != s_ring_head && count < max) {
        dest[count++] = s_ring_buf[s_ring_tail];
        s_ring_tail = (s_ring_tail + 1) % CONFIG_GADGET_LOG_RING_BUF_SIZE;
    }
    return count;
}

/* --------------------------------------------------------------------------
 * ANSI escape code stripping
 * -------------------------------------------------------------------------- */
static void ansi_strip(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        if ((unsigned char)src[i] == 0x1B && src[i + 1] == '[') {
            /* Skip ESC [ ... m */
            i += 2;
            while (src[i] != '\0' && src[i] != 'm') i++;
            /* Loop increment will move past 'm' */
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

/* --------------------------------------------------------------------------
 * Level parsing
 *   ESP-IDF log format (after ANSI strip): "X (timestamp) tag: msg\n"
 *   X = E/W/I/D/V
 * -------------------------------------------------------------------------- */
static int log_level_from_char(char c)
{
    switch (c) {
        case 'E': return 1;
        case 'W': return 2;
        case 'I': return 3;
        case 'D': return 4;
        case 'V': return 5;
        default:  return 0;
    }
}

/* --------------------------------------------------------------------------
 * File rotation
 * -------------------------------------------------------------------------- */
static void rotate_log_file(void)
{
    s_write_idx = (s_write_idx + 1) % CONFIG_GADGET_LOG_MAX_FILES;
    char path[64];
    snprintf(path, sizeof(path),
             CONFIG_GADGET_LOG_MOUNT_POINT "/logs/log_%04d.txt", s_write_idx);
    remove(path); /* evict oldest occupant if present */
    s_current_file_size = 0;
    ESP_LOGI(TAG, "Log rotated to slot %d", s_write_idx);
}

/* --------------------------------------------------------------------------
 * HTTPS offload stub
 * -------------------------------------------------------------------------- */
static esp_err_t gadget_log_http_post_stub(const uint8_t *data, size_t len)
{
    /* STUB — replace body with real esp_http_client HTTPS POST. */
    ESP_LOGI(TAG, "[OFFLOAD STUB] POST %u bytes to '%s'",
             (unsigned)len, CONFIG_GADGET_LOG_OFFLOAD_URL);
    /*
     * TODO: implement real offload, e.g.
     *   esp_http_client_config_t cfg = { .url = CONFIG_GADGET_LOG_OFFLOAD_URL };
     *   esp_http_client_handle_t h = esp_http_client_init(&cfg);
     *   esp_http_client_set_header(h, "Authorization", CONFIG_GADGET_LOG_AWS_AUTH_TOKEN);
     *   esp_http_client_set_post_field(h, (const char *)data, len);
     *   esp_err_t err = esp_http_client_perform(h);
     *   esp_http_client_cleanup(h);
     *   return err;
     */
    (void)data;
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * SNTP callback
 * -------------------------------------------------------------------------- */
static void sntp_sync_cb(struct timeval *tv)
{
    s_time_synced = true;
    char buf[32];
    struct tm tm_info;
    localtime_r(&tv->tv_sec, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_info);
    ESP_LOGI(TAG, "SNTP sync complete: %s UTC", buf);
}

/* --------------------------------------------------------------------------
 * Flush timer callback
 * -------------------------------------------------------------------------- */
static void flush_timer_cb(void *arg)
{
    (void)arg;
    gadget_log_flush();
}

/* --------------------------------------------------------------------------
 * vprintf hook — installed via esp_log_set_vprintf()
 * -------------------------------------------------------------------------- */
static int gadget_log_vprintf_hook(const char *fmt, va_list args)
{
    int ret = 0;

    /* 1. Forward to the original handler (UART / stdout) first */
    if (s_prev_vprintf != NULL) {
        va_list args_uart;
        va_copy(args_uart, args);
        ret = s_prev_vprintf(fmt, args_uart);
        va_end(args_uart);
    }

    /* 2. Format into a local buffer */
    char raw[CONFIG_GADGET_LOG_MAX_LINE_LEN];
    va_list args_fmt;
    va_copy(args_fmt, args);
    vsnprintf(raw, sizeof(raw), fmt, args_fmt);
    va_end(args_fmt);

    /* 3. Strip ANSI escape codes */
    char stripped[CONFIG_GADGET_LOG_MAX_LINE_LEN];
    ansi_strip(raw, stripped, sizeof(stripped));

    /* 4. Level filter */
    int level = log_level_from_char(stripped[0]);
    if (level == 0 || level > GADGET_LOG_WRITE_LEVEL_INT) {
        return ret;
    }

    /* 5. Optionally prepend wall-clock timestamp */
    const char *write_ptr = stripped;
    size_t write_len = strlen(stripped);

    char timestamped[CONFIG_GADGET_LOG_MAX_LINE_LEN + 40];
    if (s_time_synced) {
        struct timeval tv;
        struct tm tm_info;
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm_info);
        int ms = (int)(tv.tv_usec / 1000);
        int n = snprintf(timestamped, sizeof(timestamped),
                         "[%04d-%02d-%02dT%02d:%02d:%02d.%03d] %s",
                         tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
                         tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, ms,
                         stripped);
        if (n > 0 && n < (int)sizeof(timestamped)) {
            write_ptr = timestamped;
            write_len = (size_t)n;
        }
    }

    /* 6. Write to ring buffer under spinlock */
    portENTER_CRITICAL(&s_ring_mux);
    ring_buf_write(write_ptr, write_len);
    portEXIT_CRITICAL(&s_ring_mux);

    return ret;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

esp_err_t gadget_log_init(void)
{
    /* Mount LittleFS */
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = CONFIG_GADGET_LOG_MOUNT_POINT,
        .partition_label        = CONFIG_GADGET_LOG_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_lfs_mounted = true;

    /* Create /logs directory if absent */
    char logs_dir[64];
    snprintf(logs_dir, sizeof(logs_dir), CONFIG_GADGET_LOG_MOUNT_POINT "/logs");
    struct stat st;
    if (stat(logs_dir, &st) != 0) {
        mkdir(logs_dir, 0775);
    }

    /* Select write slot: first missing file, or slot 0 (evicting it) if full */
    s_write_idx = 0;
    for (int i = 0; i < CONFIG_GADGET_LOG_MAX_FILES; i++) {
        char path[64];
        snprintf(path, sizeof(path),
                 CONFIG_GADGET_LOG_MOUNT_POINT "/logs/log_%04d.txt", i);
        if (stat(path, &st) != 0) {
            s_write_idx = (uint16_t)i;
            break;
        }
        if (i == CONFIG_GADGET_LOG_MAX_FILES - 1) {
            /* All slots occupied — evict slot 0 */
            s_write_idx = 0;
            snprintf(path, sizeof(path),
                     CONFIG_GADGET_LOG_MOUNT_POINT "/logs/log_0000.txt");
            remove(path);
        }
    }
    s_current_file_size = 0;
    ESP_LOGI(TAG, "LittleFS mounted at %s, write slot %d",
             CONFIG_GADGET_LOG_MOUNT_POINT, s_write_idx);

    /* File mutex */
    s_file_mutex = xSemaphoreCreateMutex();
    if (s_file_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create file mutex");
        esp_vfs_littlefs_unregister(CONFIG_GADGET_LOG_PARTITION_LABEL);
        s_lfs_mounted = false;
        return ESP_FAIL;
    }

    /* Install vprintf hook */
    s_prev_vprintf = esp_log_set_vprintf(gadget_log_vprintf_hook);

    /* Create and start periodic flush timer */
    esp_timer_create_args_t timer_args = {
        .callback = flush_timer_cb,
        .name     = "log_flush",
    };
    ret = esp_timer_create(&timer_args, &s_flush_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_timer_start_periodic(s_flush_timer,
                                   (uint64_t)CONFIG_GADGET_LOG_FLUSH_INTERVAL_MS * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "gadget_log ready (flush every %d ms, write level %d)",
             CONFIG_GADGET_LOG_FLUSH_INTERVAL_MS, GADGET_LOG_WRITE_LEVEL_INT);
    return ESP_OK;
}

void gadget_log_flush(void)
{
    if (!s_lfs_mounted || s_file_mutex == NULL) return;

    /* Drain ring buffer under spinlock into a local buffer */
    char drain_buf[CONFIG_GADGET_LOG_RING_BUF_SIZE];
    size_t drained;

    portENTER_CRITICAL(&s_ring_mux);
    drained = ring_buf_drain(drain_buf, sizeof(drain_buf));
    portEXIT_CRITICAL(&s_ring_mux);

    if (drained == 0) return;

    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    char path[64];
    snprintf(path, sizeof(path),
             CONFIG_GADGET_LOG_MOUNT_POINT "/logs/log_%04d.txt", s_write_idx);

    FILE *f = fopen(path, "a");
    if (f != NULL) {
        fwrite(drain_buf, 1, drained, f);
        fclose(f);
        s_current_file_size += drained;
        if (s_current_file_size >= (size_t)CONFIG_GADGET_LOG_MAX_FILE_SIZE) {
            rotate_log_file();
        }
    } else {
        ESP_LOGE(TAG, "fopen failed: %s", path);
    }

    xSemaphoreGive(s_file_mutex);
}

void gadget_log_sntp_sync(void)
{
    if (s_time_synced) return;

    ESP_LOGI(TAG, "Starting SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
}

esp_err_t gadget_log_offload(void)
{
    if (!s_lfs_mounted || s_file_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    /* Flush ring buffer to flash before reading files */
    char drain_buf[CONFIG_GADGET_LOG_RING_BUF_SIZE];
    size_t drained;

    portENTER_CRITICAL(&s_ring_mux);
    drained = ring_buf_drain(drain_buf, sizeof(drain_buf));
    portEXIT_CRITICAL(&s_ring_mux);

    if (drained > 0) {
        char path[64];
        snprintf(path, sizeof(path),
                 CONFIG_GADGET_LOG_MOUNT_POINT "/logs/log_%04d.txt", s_write_idx);
        FILE *f = fopen(path, "a");
        if (f != NULL) {
            fwrite(drain_buf, 1, drained, f);
            fclose(f);
            s_current_file_size += drained;
        }
    }

    /* Offload each existing log file */
    bool any_failed = false;
    for (int i = 0; i < CONFIG_GADGET_LOG_MAX_FILES; i++) {
        char path[64];
        snprintf(path, sizeof(path),
                 CONFIG_GADGET_LOG_MOUNT_POINT "/logs/log_%04d.txt", i);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
        if (buf == NULL) {
            ESP_LOGE(TAG, "malloc failed (%ld B) for %s", (long)st.st_size, path);
            any_failed = true;
            continue;
        }

        FILE *f = fopen(path, "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "fopen failed: %s", path);
            free(buf);
            any_failed = true;
            continue;
        }

        size_t bytes_read = fread(buf, 1, (size_t)st.st_size, f);
        fclose(f);

        esp_err_t post_ret = gadget_log_http_post_stub(buf, bytes_read);
        free(buf);

        if (post_ret == ESP_OK) {
            remove(path);
            ESP_LOGI(TAG, "Offloaded and deleted %s", path);
        } else {
            ESP_LOGW(TAG, "Offload failed for %s, keeping file", path);
            any_failed = true;
        }
    }

    if (!any_failed) {
        s_write_idx = 0;
        s_current_file_size = 0;
    }

    xSemaphoreGive(s_file_mutex);
    return any_failed ? ESP_FAIL : ESP_OK;
}

void gadget_log_deinit(void)
{
    if (s_flush_timer != NULL) {
        esp_timer_stop(s_flush_timer);
        esp_timer_delete(s_flush_timer);
        s_flush_timer = NULL;
    }
    gadget_log_flush();
    if (s_prev_vprintf != NULL) {
        esp_log_set_vprintf(s_prev_vprintf);
        s_prev_vprintf = NULL;
    }
    if (s_file_mutex != NULL) {
        vSemaphoreDelete(s_file_mutex);
        s_file_mutex = NULL;
    }
    if (s_lfs_mounted) {
        esp_vfs_littlefs_unregister(CONFIG_GADGET_LOG_PARTITION_LABEL);
        s_lfs_mounted = false;
    }
}
