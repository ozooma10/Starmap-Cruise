#pragma once

#include <cstdint>
#include <string_view>

namespace CFS::Localization
{
    enum class Language : std::uint8_t
    {
        English,
        French,
        German,
        Spanish,
        Italian,
        Polish,
        PortugueseBrazil,
        Japanese,
        SimplifiedChinese,
    };

    enum class TextId : std::uint8_t
    {
        None,
        SetCruiseTarget,
        HoldToCruise,
        JumpThenCruise,
    };

    // Starfield's sLanguage codes; unsupported or absent values use English.
    [[nodiscard]] Language ParseLanguage(std::string_view code) noexcept;
    [[nodiscard]] std::string_view LanguageCode(Language language) noexcept;

    // Returned UTF-8 strings have static lifetime. None resolves to empty text.
    [[nodiscard]] std::string_view Translate(TextId text, Language language) noexcept;
}
