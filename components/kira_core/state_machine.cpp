/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "kira/state_machine.hpp"

namespace kira {

bool StateMachine::dispatch(Event event) noexcept
{
    if (event == Event::FatalError) {
        current_ = State::Error;
        return true;
    }
    if (current_ == State::Error) {
        if (event == Event::Recover) {
            current_ = State::Idle;
            return true;
        }
        return false;
    }

    if (event == Event::NetworkLost && current_ != State::Offline) {
        return_from_offline_ = current_ == State::Booting ? State::Idle : current_;
        current_ = State::Offline;
        return true;
    }
    if (current_ == State::Offline) {
        if (event == Event::NetworkRestored) {
            current_ = return_from_offline_;
            return true;
        }
        if (event == Event::ScreenTapped) {
            current_ = State::Desktop;
            return true;
        }
        return false;
    }

    switch (current_) {
    case State::Booting:
        if (event == Event::BootCompleted) current_ = State::Idle;
        else return false;
        break;
    case State::Idle:
        if (event == Event::WakeWordDetected) current_ = State::Listening;
        else if (event == Event::ScreenTapped) current_ = State::Desktop;
        else return false;
        break;
    case State::Listening:
        if (event == Event::SpeechCommitted) current_ = State::Thinking;
        else if (event == Event::ScreenTapped) current_ = State::Desktop;
        else return false;
        break;
    case State::Thinking:
        if (event == Event::ResponseStarted) current_ = State::Speaking;
        else if (event == Event::ScreenTapped) current_ = State::Desktop;
        else return false;
        break;
    case State::Speaking:
        if (event == Event::ResponseFinished) current_ = State::Idle;
        else if (event == Event::WakeWordDetected) current_ = State::Listening;
        else if (event == Event::ScreenTapped) current_ = State::Desktop;
        else return false;
        break;
    case State::Desktop:
        if (event == Event::DesktopDismissed) current_ = State::Idle;
        else if (event == Event::WakeWordDetected) current_ = State::Listening;
        else return false;
        break;
    case State::Offline:
    case State::Error:
        return false;
    }
    return true;
}

const char *to_string(State state) noexcept
{
    switch (state) {
    case State::Booting: return "booting";
    case State::Idle: return "idle";
    case State::Listening: return "listening";
    case State::Thinking: return "thinking";
    case State::Speaking: return "speaking";
    case State::Desktop: return "desktop";
    case State::Offline: return "offline";
    case State::Error: return "error";
    }
    return "unknown";
}

} // namespace kira

