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

constexpr size_t MAX_STATES_BODY = 768 * 1024;
constexpr std::array<std::string_view, 4> DOMAINS{"light.", "switch.", "input_boolean.", "fan."};

std::mutex s_mutex;
Status s_status;
struct Credentials {
    std::string url;
    std::string token;
    bool operator==(const Credentials &) const = default;
};
Credentials s_connection_credentials;
Credentials s_entities_credentials;

Credentials credentials()
{
    return {Settings::instance().ha_url(), Settings::instance().ha_token()};
}

void publish(const std::function<void(Status &)> &update)
{
    std::lock_guard lock(s_mutex);
    update(s_status);
    s_status.configured = !Settings::instance().ha_url().empty() && Settings::instance().has_ha_token();
    ++s_status.generation;
}

detail::HttpRequest make_request(const Credentials &config, const std::string &path)
{
    detail::HttpRequest request;
    request.url = config.url + path;
    request.headers.emplace_back("Authorization", "Bearer " + config.token);
    request.headers.emplace_back("Content-Type", "application/json");
    // A redirect must not forward a long-lived token to another host.
    request.follow_redirects = false;
    return request;
}

bool ready(const Credentials &config, const char *action)
{
    if (!config.url.empty() && !config.token.empty()) {
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

void test_job(const Credentials &config)
{
    if (!ready(config, "testing")) {
        return;
    }
    const auto response = detail::http_fetch(make_request(config, "/api/"), 4096);
    const bool ok = response.err == ESP_OK && response.body.find("API running") != std::string::npos;
    publish([&](Status &s) {
        s.busy = false;
        s.connected = ok;
        s.message = ok ? "Connected to Home Assistant" : describe_failure(response);
    });
}

void refresh_job(const Credentials &config)
{
    if (!ready(config, "loading devices")) {
        return;
    }
    const auto response = detail::http_fetch(make_request(config, "/api/states"), MAX_STATES_BODY);
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
            s.connected = false;
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
        s_entities_credentials = config;
        s.message = count == 0 ? "No lights or switches found" : std::to_string(count) + " devices";
    });
}

void toggle_job(const Credentials &config, const std::string &entity_id)
{
    if (!ready(config, "switching")) {
        return;
    }
    auto request = make_request(config, "/api/services/homeassistant/toggle");
    request.method = HTTP_METHOD_POST;
    request.body = "{\"entity_id\":\"" + entity_id + "\"}";
    const auto response = detail::http_fetch(request, 64 * 1024);
    if (response.err != ESP_OK) {
        publish([&](Status &s) {
            s.busy = false;
            s.connected = false;
            s.message = describe_failure(response);
        });
        return;
    }
    // Read back the new state of this entity only.
    const auto state = detail::http_fetch(make_request(config, "/api/states/" + entity_id), 16 * 1024);
    cJSON *root = state.err == ESP_OK ? cJSON_Parse(state.body.c_str()) : nullptr;
    const Entity updated = root != nullptr ? parse_entity(root) : Entity{};
    cJSON_Delete(root);
    publish([&](Status &s) {
        s.busy = false;
        s.connected = state.err == ESP_OK && updated.entity_id == entity_id && !updated.state.empty();
        for (auto &entity : s.entities) {
            if (entity.entity_id == entity_id && s.connected) {
                entity.state = updated.state;
            }
        }
        s.message = s.connected ? "State refreshed" : "Toggle sent; refresh failed. Refresh before toggling again.";
    });
}

void run(const char *name, std::function<void(const Credentials &)> job)
{
    const auto config = credentials();
    {
        std::lock_guard lock(s_mutex);
        if (s_status.busy) {
            return;
        }
        s_status.busy = true;
        s_connection_credentials = config;
        ++s_status.generation;
    }
    if (!submit_job(name, [job = std::move(job), config] { job(config); })) {
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
    const auto config = credentials();
    copy.configured = !config.url.empty() && !config.token.empty();
    if (!(config == s_connection_credentials)) {
        copy.connected = false;
        copy.message = "Configuration changed; test the connection";
    }
    if (!(config == s_entities_credentials)) {
        copy.entities.clear();
    }
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
    if (!valid || !is_toggleable(entity_id)) {
        return;
    }
    const auto expected_config = credentials();
    {
        std::lock_guard lock(s_mutex);
        if (!(expected_config == s_entities_credentials) || !s_status.connected) {
            return;
        }
        const auto entity = std::find_if(s_status.entities.begin(), s_status.entities.end(),
            [&](const Entity &item) { return item.entity_id == entity_id; });
        if (entity == s_status.entities.end() || (entity->state != "on" && entity->state != "off")) {
            return;
        }
    }
    run("ha_toggle", [entity_id, expected_config](const Credentials &config) {
        if (!(config == expected_config)) {
            publish([](Status &s) {
                s.busy = false;
                s.connected = false;
                s.message = "Configuration changed; refresh devices";
            });
            return;
        }
        toggle_job(config, entity_id);
    });
}

}  // namespace kira::platform::home_assistant
