/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/home_assistant.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <string_view>

#include "cJSON.h"
#include "esp_log.h"

#include "http_util.hpp"
#include "kira/platform/settings.hpp"
#include "kira/platform/worker.hpp"

namespace kira::platform::home_assistant {
namespace {

constexpr const char *TAG = "kira_ha";
constexpr size_t MAX_STATES_BODY = 768 * 1024;
constexpr std::array<std::string_view, 4> DOMAINS{"light.", "switch.", "input_boolean.", "fan."};

std::mutex s_mutex;
Status s_status;

void publish(const std::function<void(Status &)> &update)
{
    std::lock_guard lock(s_mutex);
    update(s_status);
    s_status.configured = !Settings::instance().ha_url().empty() && Settings::instance().has_ha_token();
    ++s_status.generation;
}

detail::HttpRequest make_request(const std::string &path)
{
    detail::HttpRequest request;
    request.url = Settings::instance().ha_url() + path;
    request.headers.emplace_back("Authorization", "Bearer " + Settings::instance().ha_token());
    request.headers.emplace_back("Content-Type", "application/json");
    return request;
}

bool ready(const char *action)
{
    if (!Settings::instance().ha_url().empty() && Settings::instance().has_ha_token()) {
        return true;
    }
    publish([action](Status &s) {
        s.busy = false;
        s.connected = false;
        s.message = std::string("Set the address and token before ") + action;
    });
    return false;
}

std::string describe_failure(const detail::HttpResponse &response)
{
    if (response.status == 401) {
        return "Token rejected (401)";
    }
    if (response.status != 0) {
        return "Home Assistant answered " + std::to_string(response.status);
    }
    return std::string("Cannot reach Home Assistant: ") + esp_err_to_name(response.err);
}

bool is_toggleable(std::string_view entity_id)
{
    return std::any_of(DOMAINS.begin(), DOMAINS.end(),
                       [entity_id](std::string_view domain) { return entity_id.starts_with(domain); });
}

std::string json_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

Entity parse_entity(const cJSON *item)
{
    Entity entity;
    entity.entity_id = json_string(item, "entity_id");
    entity.state = json_string(item, "state");
    const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    entity.name = attributes != nullptr ? json_string(attributes, "friendly_name") : "";
    if (entity.name.empty()) {
        entity.name = entity.entity_id;
    }
    return entity;
}

void test_job()
{
    if (!ready("testing")) {
        return;
    }
    const auto response = detail::http_fetch(make_request("/api/"), 4096);
    const bool ok = response.err == ESP_OK && response.body.find("API running") != std::string::npos;
    publish([&](Status &s) {
        s.busy = false;
        s.connected = ok;
        s.message = ok ? "Connected to Home Assistant" : describe_failure(response);
    });
}

void refresh_job()
{
    if (!ready("loading devices")) {
        return;
    }
    const auto response = detail::http_fetch(make_request("/api/states"), MAX_STATES_BODY);
    if (response.err != ESP_OK) {
        publish([&](Status &s) {
            s.busy = false;
            s.connected = false;
            s.message = response.truncated ? "Too many entities to load" : describe_failure(response);
        });
        return;
    }
    cJSON *root = cJSON_Parse(response.body.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        publish([](Status &s) {
            s.busy = false;
            s.message = "Unexpected answer from Home Assistant";
        });
        return;
    }
    std::vector<Entity> entities;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        const std::string id = json_string(item, "entity_id");
        if (!is_toggleable(id)) {
            continue;
        }
        entities.push_back(parse_entity(item));
        if (entities.size() >= MAX_ENTITIES) {
            break;
        }
    }
    cJSON_Delete(root);
    std::sort(entities.begin(), entities.end(),
              [](const Entity &a, const Entity &b) { return a.name < b.name; });
    const size_t count = entities.size();
    publish([&](Status &s) {
        s.busy = false;
        s.connected = true;
        s.entities = std::move(entities);
        s.message = count == 0 ? "No lights or switches found" : std::to_string(count) + " devices";
    });
}

void toggle_job(const std::string &entity_id)
{
    if (!ready("switching")) {
        return;
    }
    auto request = make_request("/api/services/homeassistant/toggle");
    request.method = HTTP_METHOD_POST;
    request.body = "{\"entity_id\":\"" + entity_id + "\"}";
    const auto response = detail::http_fetch(request, 64 * 1024);
    if (response.err != ESP_OK) {
        publish([&](Status &s) {
            s.busy = false;
            s.message = describe_failure(response);
        });
        return;
    }
    // Read back the new state of this entity only.
    const auto state = detail::http_fetch(make_request("/api/states/" + entity_id), 16 * 1024);
    cJSON *root = state.err == ESP_OK ? cJSON_Parse(state.body.c_str()) : nullptr;
    const Entity updated = root != nullptr ? parse_entity(root) : Entity{};
    cJSON_Delete(root);
    publish([&](Status &s) {
        s.busy = false;
        s.connected = true;
        for (auto &entity : s.entities) {
            if (entity.entity_id == entity_id && !updated.state.empty()) {
                entity.state = updated.state;
            }
        }
        s.message.clear();
    });
}

void run(const char *name, std::function<void()> job)
{
    publish([](Status &s) { s.busy = true; });
    if (!submit_job(name, std::move(job))) {
        publish([](Status &s) {
            s.busy = false;
            s.message = "Busy, try again";
        });
    }
}

}  // namespace

Status status()
{
    std::lock_guard lock(s_mutex);
    Status copy = s_status;
    copy.configured = !Settings::instance().ha_url().empty() && Settings::instance().has_ha_token();
    return copy;
}

void test_async()
{
    run("ha_test", test_job);
}

void refresh_async()
{
    run("ha_refresh", refresh_job);
}

void toggle_async(const std::string &entity_id)
{
    // entity ids are [a-z0-9_.]; refuse anything else before building JSON.
    const bool valid = !entity_id.empty() && std::all_of(entity_id.begin(), entity_id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    });
    if (!valid) {
        return;
    }
    run("ha_toggle", [entity_id] { toggle_job(entity_id); });
}

}  // namespace kira::platform::home_assistant
