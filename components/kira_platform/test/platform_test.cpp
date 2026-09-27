/* SPDX-License-Identifier: Apache-2.0 */
// Host tests for the hardware-independent parts of kira_platform.
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "kira/platform/ai_providers.hpp"
#include "kira/platform/semver.hpp"

using namespace kira::platform;

static int cmp(const char *a, const char *b)
{
    const auto va = parse_semver(a);
    const auto vb = parse_semver(b);
    assert(va && vb);
    return compare_semver(*va, *vb);
}

static void test_semver()
{
    assert(parse_semver("0.2.0"));
    assert(parse_semver("v1.2.3"));
    assert(parse_semver("1.2.3-rc.1+abc1234"));
    assert(parse_semver("0.2.0+5baa2d6")->to_string() == "0.2.0");
    assert(!parse_semver(""));
    assert(!parse_semver("1.2"));
    assert(!parse_semver("1.2.3.4"));
    assert(!parse_semver("01.2.3"));
    assert(!parse_semver("1.2.x"));
    assert(!parse_semver("1.2.3-"));
    assert(!parse_semver("1.2.3-rc..1"));

    assert(cmp("0.2.0", "0.1.9") > 0);
    assert(cmp("0.10.0", "0.9.9") > 0);
    assert(cmp("1.0.0", "1.0.0+other") == 0);
    assert(cmp("1.0.0", "1.0.0-rc.1") > 0);
    assert(cmp("1.0.0-alpha", "1.0.0-alpha.1") < 0);
    assert(cmp("1.0.0-alpha.1", "1.0.0-alpha.beta") < 0);
    assert(cmp("1.0.0-beta.2", "1.0.0-beta.11") < 0);
    assert(cmp("1.0.0-rc.1", "1.0.0-beta.11") > 0);
    assert(cmp("v0.2.1", "0.2.0+sha") > 0);
}

static void test_providers()
{
    std::set<std::string> ids;
    for (const auto &provider : ai_providers()) {
        assert(!provider.id.empty() && !provider.display_name.empty());
        // stored as NVS key "key_<id>", NVS keys are at most 15 characters
        assert(provider.id.size() <= 11);
        assert(ids.insert(std::string(provider.id)).second);
        assert(find_ai_provider(provider.id) == &provider);
    }
    assert(find_ai_provider("nope") == nullptr);
    assert(default_ai_provider().id == "openai");
}

int main()
{
    test_semver();
    test_providers();
    std::puts("kira_platform host tests: OK");
    return 0;
}
