#ifndef GADGET_LOG_H
#define GADGET_LOG_H

#include "esp_err.h"

/**
 * @brief Initialize the gadget log library.
 *
 * Mounts LittleFS, creates the file mutex, installs the vprintf hook, and
 * starts the periodic flush timer.  Must be called after nvs_flash_init()
 * and before init_msg_queues() / init_tasks() so that early log output from
 * task-creation is captured.
 *
 * @return ESP_OK on success, ESP_FAIL if LittleFS mount or mutex creation fails.
 */
esp_err_t gadget_log_init(void);

/**
 * @brief Drain the RAM ring buffer to the current LittleFS log file.
 *
 * Thread-safe (uses the file mutex internally).  Rotates to the next file
 * slot when the current file exceeds CONFIG_GADGET_LOG_MAX_FILE_SIZE bytes.
 * Called automatically by the flush timer and inline by gadget_log_offload().
 */
void gadget_log_flush(void);

/**
 * @brief Start SNTP synchronisation after a successful STA WiFi connection.
 *
 * Registers a sync callback that sets the internal time-synced flag.  Once
 * the flag is set, every log line written to flash is prefixed with an
 * ISO-8601 wall-clock timestamp.  Safe to call more than once — re-entry is
 * guarded by the time-synced flag.
 *
 * Call from gadget_comms_task after gadget_sta_init() returns true.
 */
void gadget_log_sntp_sync(void);

/**
 * @brief Flush ring buffer, POST every log file to the cloud stub, then
 *        delete files that were successfully offloaded.
 *
 * Blocking — must be called from a task context (not an ISR or timer
 * callback).  Designed to be triggered by gadget_msg_log_offload via the
 * gadget_comms_task.
 *
 * @return ESP_OK  if all files were offloaded and removed,
 *         ESP_FAIL if any file could not be offloaded (those files are kept).
 *         ESP_ERR_INVALID_STATE if the library has not been initialised.
 */
esp_err_t gadget_log_offload(void);

/**
 * @brief Stop the flush timer, flush remaining data, and unmount LittleFS.
 *
 * Optional — intended for clean shutdown or unit-test teardown.
 */
void gadget_log_deinit(void);

#endif /* GADGET_LOG_H */
