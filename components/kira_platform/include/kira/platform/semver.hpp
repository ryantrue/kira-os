/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kira::platform {

// Semantic version MAJOR.MINOR.PATCH[-prerelease][+build].
// Build metadata is ignored for ordering, as SemVer 2.0 requires.
struct SemVer {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    std::string prerelease;  // empty for a release

    [[nodiscard]] std::string to_string() const;
};

// Accepts an optional leading 'v' ("v1.2.3", "1.2.3-rc.1+abc123").
[[nodiscard]] std::optional<SemVer> parse_semver(std::string_view text);

// <0 when a < b, 0 when equal in precedence, >0 when a > b.
[[nodiscard]] int compare_semver(const SemVer &a, const SemVer &b);

}  // namespace kira::platform
