#pragma once
#include "engine/engine.h"
#include <functional>
namespace wi {
bool startCapture(Engine &engine, std::function<void()> showWindow,
                  std::function<void()> requestExit);
void stopCapture();
} // namespace wi
