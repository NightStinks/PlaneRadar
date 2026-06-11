#pragma once
#include <functional>
#include "config.h"

void setup_portal_run(AppConfig &cfg, std::function<void()> tick_fn = nullptr);
