/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cassert>
#include "kira/state_machine.hpp"

int main()
{
    kira::StateMachine state;
    assert(state.current() == kira::State::Booting);
    assert(state.dispatch(kira::Event::BootCompleted));
    assert(state.current() == kira::State::Idle);
    assert(state.dispatch(kira::Event::WakeWordDetected));
    assert(state.current() == kira::State::Listening);
    assert(state.dispatch(kira::Event::SpeechCommitted));
    assert(state.current() == kira::State::Thinking);
    assert(state.dispatch(kira::Event::ResponseStarted));
    assert(state.current() == kira::State::Speaking);
    assert(state.dispatch(kira::Event::ResponseFinished));
    assert(state.current() == kira::State::Idle);
    assert(state.dispatch(kira::Event::ScreenTapped));
    assert(state.current() == kira::State::Desktop);
    assert(state.dispatch(kira::Event::NetworkLost));
    assert(state.current() == kira::State::Offline);
    assert(state.dispatch(kira::Event::NetworkRestored));
    assert(state.current() == kira::State::Desktop);
    assert(state.dispatch(kira::Event::DesktopDismissed));
    assert(state.current() == kira::State::Idle);
    assert(!state.dispatch(kira::Event::ResponseStarted));
}

