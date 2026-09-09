#include "Presentation/Localization.h"
#include "TestSuites.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    using namespace CFS::Localization;

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    struct LanguageCase
    {
        std::string_view code;
        Language language;
        std::string_view cruiseTerm;
    };

    constexpr std::array Languages {
        LanguageCase {"en", Language::English, "CRUISE"},
        LanguageCase {"fr", Language::French, "CROISIÈRE"},
        LanguageCase {"de", Language::German, "REISEMODUS"},
        LanguageCase {"es", Language::Spanish, "CRUCERO"},
        LanguageCase {"it", Language::Italian, "CROCIERA"},
        LanguageCase {"pl", Language::Polish, "REJS"},
        LanguageCase {"ptbr", Language::PortugueseBrazil, "CRUZEIRO"},
        LanguageCase {"ja", Language::Japanese, "クルーズ"},
        LanguageCase {"zhhans", Language::SimplifiedChinese, "巡航"},
    };

    constexpr std::array Labels {TextId::SetCruiseTarget, TextId::HoldToCruise, TextId::JumpThenCruise};

    bool IsValidUtf8(std::string_view text)
    {
        std::size_t index = 0;
        while (index < text.size()) {
            const auto lead = static_cast<unsigned char>(text[index++]);
            if (lead <= 0x7F)
                continue;

            std::uint32_t codePoint;
            std::uint32_t minimum;
            std::size_t continuationCount;
            if (lead >= 0xC2 && lead <= 0xDF) {
                codePoint = lead & 0x1F;
                minimum = 0x80;
                continuationCount = 1;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                codePoint = lead & 0x0F;
                minimum = 0x800;
                continuationCount = 2;
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                codePoint = lead & 0x07;
                minimum = 0x10000;
                continuationCount = 3;
            } else {
                return false;
            }

            if (continuationCount > text.size() - index)
                return false;
            while (continuationCount-- > 0) {
                const auto byte = static_cast<unsigned char>(text[index++]);
                if ((byte & 0xC0) != 0x80)
                    return false;
                codePoint = (codePoint << 6) | (byte & 0x3F);
            }
            if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                return false;
        }
        return true;
    }

    void TestLanguageSelection()
    {
        for (const auto& item : Languages) {
            Require(ParseLanguage(item.code) == item.language, "game language code resolved incorrectly");
            Require(LanguageCode(item.language) == item.code, "language code did not round-trip");
            auto upper = std::string {item.code};
            for (auto& letter : upper)
                letter = static_cast<char>(letter - ('a' - 'A'));
            Require(ParseLanguage(upper) == item.language, "uppercase game language code resolved incorrectly");
            Require(ParseLanguage(" \t\r\n" + upper + "\v\f ") == item.language, "surrounding whitespace changed language selection");
        }
        Require(ParseLanguage("pTbR") == Language::PortugueseBrazil, "mixed-case language was not recognized");
        Require(ParseLanguage("ZhHaNs") == Language::SimplifiedChinese, "mixed-case Chinese code was not recognized");

        constexpr std::array<std::string_view, 10> unsupported {
            "", " \t\r\n\v\f", "ru", "en-US", "pt-BR", "zh-CN", "german", "de extra", "d e", "\xFF",
        };
        for (const auto code : unsupported)
            Require(ParseLanguage(code) == Language::English, "missing or unsupported language did not fall back to English");
    }

    void TestCatalogCompletenessAndEncoding()
    {
        for (const auto& item : Languages) {
            Require(Translate(TextId::None, item.language).empty(), "None returned a visible label");
            for (const auto id : Labels) {
                const auto text = Translate(id, item.language);
                Require(!text.empty(), "catalog is missing an action label");
                Require(IsValidUtf8(text), "label is not valid UTF-8");
                Require(text.find('\0') == std::string_view::npos, "label contains an embedded NUL");
                Require(text.find(item.cruiseTerm) != std::string_view::npos, "label uses the wrong language or Cruise terminology");
                if (item.language != Language::English)
                    Require(text != Translate(id, Language::English), "non-English label fell back to English");
            }
        }

        // Byte expectations catch accidental dependence on the compiler's system code page.
        Require(Translate(TextId::SetCruiseTarget, Language::German).find("F\xC3\x9CR") != std::string_view::npos, "German umlaut was not encoded as UTF-8");
        Require(Translate(TextId::SetCruiseTarget, Language::SimplifiedChinese) ==
            "\xE8\xAE\xBE\xE7\xBD\xAE\xE5\xB7\xA1\xE8\x88\xAA\xE7\x9B\xAE\xE6\xA0\x87",
            "Chinese label did not retain its UTF-8 bytes");
        Require(!IsValidUtf8("\xC0\xAF") && !IsValidUtf8("\xED\xA0\x80") && !IsValidUtf8("\xF4\x90\x80\x80") && !IsValidUtf8("\xE8\xAE"),
            "encoding check accepted invalid Unicode");
    }

    void TestEnglishCompatibilityAndFallback()
    {
        constexpr std::array<std::string_view, 3> expected {"SET CRUISE TARGET", "HOLD TO CRUISE", "JUMP THEN CRUISE"};
        for (std::size_t index = 0; index < Labels.size(); ++index) {
            Require(Translate(Labels[index], Language::English) == expected[index], "English wording changed");
            Require(Translate(Labels[index], ParseLanguage("unsupported")) == expected[index], "fallback wording changed");
            Require(Translate(Labels[index], static_cast<Language>(0xFF)) == expected[index], "invalid language did not safely fall back");
        }
        Require(LanguageCode(static_cast<Language>(0xFF)) == "en", "invalid language logged the wrong code");
        Require(Translate(static_cast<TextId>(0xFF), Language::English).empty(), "unknown text ID produced a label");
    }
}

void RunLocalizationTests()
{
    TestLanguageSelection();
    TestCatalogCompletenessAndEncoding();
    TestEnglishCompatibilityAndFallback();
}
