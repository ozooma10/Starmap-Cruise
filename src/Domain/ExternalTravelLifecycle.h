#pragma once

#include <cstdint>

enum class ExternalTravelTransition : std::uint8_t
{
    None,
    Started,
    Ended,
};

class ExternalTravelLifecycle final
{
public:
    void BeginOperation(std::uint64_t operationId, bool activeAtBaseline) noexcept
    {
        m_operationId = operationId;
        m_sawInactive = !activeAtBaseline;
        m_active = activeAtBaseline;
        m_started = false;
        m_ended = false;
    }

    ExternalTravelTransition Observe(std::uint64_t operationId, bool active) noexcept
    {
        if (operationId == 0 || operationId != m_operationId) {
            return ExternalTravelTransition::None;
        }

        if (!m_started) {
            m_active = active;
            if (!active) {
                m_sawInactive = true;
                return ExternalTravelTransition::None;
            }
            if (!m_sawInactive) {
                return ExternalTravelTransition::None;
            }
            m_started = true;
            return ExternalTravelTransition::Started;
        }

        if (!m_ended && m_active && !active) {
            m_active = false;
            m_ended = true;
            return ExternalTravelTransition::Ended;
        }
        m_active = active;
        return ExternalTravelTransition::None;
    }

    void Reset() noexcept
    {
        m_operationId = 0;
        m_sawInactive = false;
        m_active = false;
        m_started = false;
        m_ended = false;
    }

    [[nodiscard]] bool Ended(std::uint64_t operationId) const noexcept
    {
        return operationId != 0 && operationId == m_operationId && m_ended;
    }

private:
    std::uint64_t m_operationId {0};
    bool m_sawInactive {false};
    bool m_active {false};
    bool m_started {false};
    bool m_ended {false};
};
