#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

class MapActionInputState final
{
public:
    void Begin(std::uint32_t device, std::int32_t idCode);
    std::optional<std::uint32_t> AcceptAction();
    bool Filter(std::uint32_t device, std::int32_t idCode, bool down);
    void Reset();

private:
    struct State
    {
        std::uint32_t device {0};
        std::int32_t idCode {0};
        bool claimed {false};
    };

    std::mutex m_mutex;
    std::optional<State> m_state;
};
