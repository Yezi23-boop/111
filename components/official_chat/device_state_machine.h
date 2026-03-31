#pragma once

#include "device_state.h"

namespace official_chat {

class DeviceStateMachine {
 public:
  DeviceStateMachine() = default;

  bool TransitionTo(DeviceState state);
  DeviceState GetState() const;

 private:
  DeviceState state_ = DeviceState::kUnknown;
};

}  // namespace official_chat
