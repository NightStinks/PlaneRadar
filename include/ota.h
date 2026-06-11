#pragma once
#include <functional>

using OtaProgressFn = std::function<void(int current, int total)>;

// Check for a newer firmware version and flash it if available.
// version_url: JSON endpoint {"version":"v1.2","url":"https://...firmware.bin"}
// on_progress(current_bytes, total_bytes): called during download; total=0 at start.
// Returns false always — on successful flash the device reboots automatically.
bool ota_check(OtaProgressFn on_progress = nullptr);
