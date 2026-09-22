/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

namespace kira {

enum class State : std::uint8_t {
    Booting,
    Idle,
    Listening,
    Thinking,
    Speaking,
    Desktop,
    Offline,
    Error,
};

enum class Event : std::uint8_t {
    BootCompleted,
    WakeWordDetected,
    SpeechCommitted,
    ResponseStarted,
    ResponseFinished,
    ScreenTapped,
    DesktopDismissed,
    NetworkLost,
    NetworkRestored,
    FatalError,
    Recover,
};

class StateMachine final {
public:
    [[nodiscard]] State current() const noexcept { return current_; }
    [[nodiscard]] bool dispatch(Event event) noexcept;

private:
    State current_ {State::Booting};
    State return_from_offline_ {State::Idle};
};

[[nodiscard]] const char *to_string(State state) noexcept;

} // namespace kira

