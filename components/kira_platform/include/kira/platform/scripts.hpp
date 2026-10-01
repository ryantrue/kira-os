/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace kira::platform::scripts {
enum class StepKind { WakeOnLan, Http };
struct Step { StepKind kind; std::string target; std::string argument; };
struct Script { std::string id; std::string name; std::string description; std::vector<Step> steps; bool configured=true; };
struct RunStatus { bool running=false; bool success=false; std::string script_id; std::string message; uint32_t completed_steps=0; uint32_t total_steps=0; };
const std::vector<Script>& catalog();
bool run_async(const std::string& id);
RunStatus status();
}
