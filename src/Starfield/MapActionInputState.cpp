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

    m_state->claimed = true;
    return m_state->device;
}

bool MapActionInputState::Filter(std::uint32_t device, std::int32_t idCode, bool down)
{
    std::lock_guard lock {m_mutex};
    if (!m_state || m_state->device != device || m_state->idCode != idCode) {
        return false;
    }

    const bool claimed = m_state->claimed;
    if (!down) {
        m_state.reset();
    }
    return claimed;
}

void MapActionInputState::Reset()
{
    std::lock_guard lock {m_mutex};
    m_state.reset();
}
