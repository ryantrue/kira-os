/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

class GeneralServices final {
public:
    static GeneralServices &get_instance();
    bool init();
    bool start_audio_services();

private:
    GeneralServices() = default;
    bool configure_audio();
};

