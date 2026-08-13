#include "Bodies/PluginFormId.h"

#include <utility>

namespace
{
    constexpr FormID FullLocalMask = 0x00FFFFFF;
    constexpr FormID MediumLocalMask = 0x0000FFFF;
    constexpr FormID SmallLocalMask = 0x00000FFF;

    std::optional<FormID> EncodeRuntimeFormId(FormID localId, const PluginLoadIdentity& owner)
    {
        if (!owner.IsValid()) {
            return std::nullopt;
        }

        switch (owner.tier) {
        case PluginTier::Full:
            if (localId > FullLocalMask) {
                return std::nullopt;
            }

            return (static_cast<FormID>(owner.index) << 24) | localId;

        case PluginTier::Medium:
            if (localId > MediumLocalMask) {
                return std::nullopt;
            }

            return 0xFD000000 | (static_cast<FormID>(owner.index) << 16) | localId;

        case PluginTier::Small:
            if (localId > SmallLocalMask) {
                return std::nullopt;
            }

            return
                0xFE000000 | (static_cast<FormID>(owner.index) << 12) | localId;
        }

        return std::nullopt;
    }
}

bool PluginLoadIdentity::IsValid() const
{
    switch (tier) {
    case PluginTier::Full:
        // FD, FE, and FF are reserved for other FormID spaces.
        return index <= 0xFC;

    case PluginTier::Medium:
        return index <= 0xFF;

    case PluginTier::Small:
        return index <= 0xFFF;
    }

    return false;
}

PluginFormIdResolver::PluginFormIdResolver(std::vector<PluginLoadIdentity> masters, PluginLoadIdentity self) :
    masters_(std::move(masters)), self_(self) {}

bool PluginFormIdResolver::IsValid() const
{
    if (!self_.IsValid() || masters_.size() > 0xFF) {
        return false;
    }

    for (const auto& master : masters_) {
        if (!master.IsValid()) {
            return false;
        }
    }

    return true;
}

std::optional<FormID> PluginFormIdResolver::Resolve(FormID pluginFormId) const
{
    // Zero is the explicit null FormID.
    if (pluginFormId == 0) {
        return FormID{ 0 };
    }

    if (!IsValid()) {
        return std::nullopt;
    }

    const auto ownerSlot = static_cast<std::size_t>(pluginFormId >> 24);

    const PluginLoadIdentity* owner = nullptr;

    if (ownerSlot < masters_.size()) {
        owner = &masters_[ownerSlot];
    } else if (ownerSlot == masters_.size()) {
        owner = &self_;
    } else {
        // Do not silently reinterpret an unknown master slot as self.
        return std::nullopt;
    }

    const FormID localId = pluginFormId & 0x00FFFFFF;

    return EncodeRuntimeFormId(localId, *owner);
}