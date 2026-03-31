#include "device_state_machine.h"

namespace official_chat {

bool DeviceStateMachine::TransitionTo(DeviceState state) {
  if (state_ == state) {
    return false;
  }
  state_ = state;
  return true;
}

DeviceState DeviceStateMachine::GetState() const {
  return state_;
}

}  // namespace official_chat
