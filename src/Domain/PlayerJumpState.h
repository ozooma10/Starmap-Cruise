#pragma once

#include <cstdint>

class PlayerJumpState final
{
public:
    void Observe(std::uint32_t state) noexcept
    {
        if (state == 0) {
            m_progress = 1;
            m_started = true;
            m_completed = false;
        } else if (state == 1 && m_progress == 1) {
            m_progress = 2;
        } else if (state == 2 && m_progress == 2) {
            m_progress = 0;
            m_completed = true;
        } else {
            m_progress = 0;
            m_completed = false;
        }
    }

    void Reset() noexcept
    {
        m_progress = 0;
        m_started = false;
        m_completed = false;
    }

    [[nodiscard]] bool Started() const noexcept { return m_started; }
    [[nodiscard]] bool Completed() const noexcept { return m_completed; }
    [[nodiscard]] bool InProgress() const noexcept { return m_started && !m_completed; }

private:
    std::uint8_t m_progress {0};
    bool m_started {false};
    bool m_completed {false};
};
