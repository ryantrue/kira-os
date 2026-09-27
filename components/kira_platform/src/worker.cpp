/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/worker.hpp"

#include <mutex>
#include <new>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace kira::platform {
namespace {

constexpr const char *TAG = "kira_worker";
constexpr uint32_t STACK_BYTES = 12 * 1024;  // TLS handshakes need headroom
constexpr UBaseType_t QUEUE_LENGTH = 24;

struct Job {
    const char *name;
    std::function<void()> work;
};

QueueHandle_t s_queue = nullptr;
std::mutex s_mutex;
std::string s_current;

void worker_task(void *)
{
    for (;;) {
        Job *job = nullptr;
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE || job == nullptr) {
            continue;
        }
        {
            std::lock_guard lock(s_mutex);
            s_current = job->name;
        }
        ESP_LOGI(TAG, "job start: %s", job->name);
        job->work();
        ESP_LOGI(TAG, "job done: %s", job->name);
        {
            std::lock_guard lock(s_mutex);
            s_current.clear();
        }
        delete job;
    }
}

bool ensure_started()
{
    std::lock_guard lock(s_mutex);
    if (s_queue != nullptr) {
        return true;
    }
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(Job *));
    if (s_queue == nullptr) {
        return false;
    }
    if (xTaskCreatePinnedToCore(worker_task, "kira_worker", STACK_BYTES, nullptr, 4, nullptr, 1) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return false;
    }
    return true;
}

}  // namespace

bool submit_job(const char *name, std::function<void()> job)
{
    if (!ensure_started()) {
        ESP_LOGE(TAG, "worker unavailable");
        return false;
    }
    auto *item = new (std::nothrow) Job{name, std::move(job)};
    if (item == nullptr) {
        return false;
    }
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropping %s", name);
        delete item;
        return false;
    }
    return true;
}

std::string current_job()
{
    std::lock_guard lock(s_mutex);
    return s_current;
}

}  // namespace kira::platform
