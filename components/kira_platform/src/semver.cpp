/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/semver.hpp"

#include <cctype>
#include <vector>

namespace kira::platform {
namespace {

std::optional<unsigned> parse_number(std::string_view text)
{
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    if (text.size() > 1 && text.front() == '0') {
        return std::nullopt;  // SemVer forbids leading zeros
    }
    unsigned value = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
        value = value * 10U + static_cast<unsigned>(c - '0');
    }
    return value;
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

bool is_numeric(std::string_view text)
{
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

int sign(int value)
{
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

int compare_identifiers(std::string_view a, std::string_view b)
{
    const bool a_num = is_numeric(a);
    const bool b_num = is_numeric(b);
    if (a_num && b_num) {
        if (a.size() != b.size()) {
            return a.size() < b.size() ? -1 : 1;
        }
        return sign(a.compare(b));
    }
    if (a_num != b_num) {
        return a_num ? -1 : 1;  // numeric identifiers have lower precedence
    }
    return sign(a.compare(b));
}

}  // namespace

std::string SemVer::to_string() const
{
    std::string text = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) {
        text += "-" + prerelease;
    }
    return text;
}

std::optional<SemVer> parse_semver(std::string_view text)
{
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    const size_t plus = text.find('+');
    if (plus != std::string_view::npos) {
        text = text.substr(0, plus);
    }
    std::string_view pre;
    const size_t dash = text.find('-');
    if (dash != std::string_view::npos) {
        pre = text.substr(dash + 1);
        text = text.substr(0, dash);
        if (pre.empty()) {
            return std::nullopt;
        }
        for (auto id : split(pre, '.')) {
            if (id.empty()) {
                return std::nullopt;
            }
        }
    }
    const auto core = split(text, '.');
    if (core.size() != 3) {
        return std::nullopt;
    }
    const auto major = parse_number(core[0]);
    const auto minor = parse_number(core[1]);
    const auto patch = parse_number(core[2]);
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    return SemVer{*major, *minor, *patch, std::string(pre)};
}

int compare_semver(const SemVer &a, const SemVer &b)
{
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return a.patch < b.patch ? -1 : 1;
    }
    if (a.prerelease.empty() || b.prerelease.empty()) {
        if (a.prerelease.empty() && b.prerelease.empty()) {
            return 0;
        }
        return a.prerelease.empty() ? 1 : -1;  // a release outranks its prereleases
    }
    const auto pa = split(a.prerelease, '.');
    const auto pb = split(b.prerelease, '.');
    for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
        const int c = compare_identifiers(pa[i], pb[i]);
        if (c != 0) {
            return c;
        }
    }
    if (pa.size() == pb.size()) {
        return 0;
    }
    return pa.size() < pb.size() ? -1 : 1;
}

}  // namespace kira::platform
