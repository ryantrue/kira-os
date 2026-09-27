/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

namespace kira::center {

// Kira Center: Kira's own settings and tools, drawn with LVGL on the top layer
// above System Super (the same layer as the Kira surface).
//
// start() adds the floating Kira button (right edge) that opens Kira Center
// and starts the assistant controller that shows the Kira surface after the
// configured inactivity period when auto-start is enabled.
// Call after System Super and the Kira surface are started.
void start();

// Opens Kira Center. LVGL thread only.
void open_locked();

}  // namespace kira::center
