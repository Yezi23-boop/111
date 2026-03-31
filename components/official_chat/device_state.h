#pragma once

namespace official_chat {

enum class DeviceState {
    kUnknown = 0,
    kActivating,
    kUpgrading,
    kIdle,
    kConnecting,
    kListening,
    kSpeaking,
};

}  // namespace official_chat
