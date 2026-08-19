#include "Starfield/MapActionInputState.h"

void MapActionInputState::Begin(std::uint32_t device, std::int32_t idCode)
{
    std::lock_guard lock {m_mutex};
    m_state = {
        .device = device,
        .idCode = idCode,
    };
}

std::optional<std::uint32_t> MapActionInputState::AcceptAction()
{
    std::lock_guard lock {m_mutex};
    if (!m_state || m_state->claimed) {
        return std::nullopt;
    }

    const auto device = m_state->device;
    if (m_state->released) {
        m_state.reset();
        return device;
    }

    m_state->claimed = true;
    return device;
}

bool MapActionInputState::Filter(std::uint32_t device, std::int32_t idCode, bool down)
{
    std::lock_guard lock {m_mutex};
    if (!m_state || m_state->device != device || m_state->idCode != idCode) {
        return false;
    }

    const bool claimed = m_state->claimed;
    if (!down) {
        if (claimed) {
            m_state.reset();
        } else {
            m_state->released = true;
        }
    }
    return claimed;
}

void MapActionInputState::ExpireReleased()
{
    std::lock_guard lock {m_mutex};
    if (m_state && m_state->released) {
        m_state.reset();
    }
}

void MapActionInputState::Reset()
{
    std::lock_guard lock {m_mutex};
    m_state.reset();
}
