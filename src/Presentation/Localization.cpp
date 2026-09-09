#include "Presentation/Localization.h"

#include <array>

namespace CFS::Localization
{
    namespace
    {
        struct Catalog
        {
            Language language;
            std::string_view code;
            std::string_view setCruiseTarget;
            std::string_view holdToCruise;
            std::string_view jumpThenCruise;
        };

        // Keep whole phrases so each language can use its own word order.
        // Cruise terminology follows the game's Interface/Translate_<code>.txt.
        constexpr std::array Catalogs {
            Catalog {Language::English, "en",
                "SET CRUISE TARGET", "HOLD TO CRUISE", "JUMP THEN CRUISE"},
            Catalog {Language::French, "fr",
                "DÉFINIR LA CIBLE DE CROISIÈRE", "MAINTENIR POUR LA CROISIÈRE", "SAUT PUIS CROISIÈRE"},
            Catalog {Language::German, "de",
                "ZIEL FÜR REISEMODUS SETZEN", "HALTEN FÜR REISEMODUS", "SPRUNG, DANN REISEMODUS"},
            Catalog {Language::Spanish, "es",
                "FIJAR DESTINO DE CRUCERO", "MANTENER PARA CRUCERO", "SALTO Y LUEGO CRUCERO"},
            Catalog {Language::Italian, "it",
                "IMPOSTA META DI CROCIERA", "TIENI PREMUTO PER LA CROCIERA", "SALTO, POI CROCIERA"},
            Catalog {Language::Polish, "pl",
                "USTAW CEL REJSU", "PRZYTRZYMAJ, ABY ROZPOCZĄĆ REJS", "SKOK, POTEM REJS"},
            Catalog {Language::PortugueseBrazil, "ptbr",
                "DEFINIR DESTINO DE CRUZEIRO", "SEGURE PARA INICIAR CRUZEIRO", "DOBRA, DEPOIS CRUZEIRO"},
            Catalog {Language::Japanese, "ja",
                "クルーズ目標を設定", "長押しでクルーズ", "ジャンプ後にクルーズ"},
            Catalog {Language::SimplifiedChinese, "zhhans",
                "设置巡航目标", "长按开始巡航", "跃迁后巡航"},
        };

        constexpr bool IsSpace(char value) noexcept
        {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
        }

        constexpr char AsciiLower(char value) noexcept
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        }

        const Catalog& GetCatalog(Language language) noexcept
        {
            for (const auto& catalog : Catalogs) {
                if (catalog.language == language)
                    return catalog;
            }
            return Catalogs.front();
        }
    }

    Language ParseLanguage(std::string_view code) noexcept
    {
        while (!code.empty() && IsSpace(code.front()))
            code.remove_prefix(1);
        while (!code.empty() && IsSpace(code.back()))
            code.remove_suffix(1);

        for (const auto& catalog : Catalogs) {
            if (code.size() != catalog.code.size())
                continue;
            bool matches = true;
            for (std::size_t index = 0; index < code.size(); ++index) {
                if (AsciiLower(code[index]) != catalog.code[index]) {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return catalog.language;
        }
        return Language::English;
    }

    std::string_view LanguageCode(Language language) noexcept
    {
        return GetCatalog(language).code;
    }

    std::string_view Translate(TextId text, Language language) noexcept
    {
        const auto& catalog = GetCatalog(language);
        switch (text) {
        case TextId::SetCruiseTarget:
            return catalog.setCruiseTarget;
        case TextId::HoldToCruise:
            return catalog.holdToCruise;
        case TextId::JumpThenCruise:
            return catalog.jumpThenCruise;
        case TextId::None:
        default:
            return {};
        }
    }
}
