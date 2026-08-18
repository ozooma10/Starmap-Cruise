#pragma once

#include "RE/Starfield.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace CFS::ScaleformValue
{
    using Value = RE::Scaleform::GFx::Value;
    using Params = RE::Scaleform::GFx::FunctionHandler::Params;

    [[nodiscard]] double AsNumber(const Value& a_value);
    [[nodiscard]] std::uint32_t UIntMember(Value& a_object, const char* a_name);
    [[nodiscard]] std::string StringMember(Value& a_object, const char* a_name);
    [[nodiscard]] bool ObjectMember(Value& a_object, const char* a_name, Value& a_member);
    [[nodiscard]] bool BooleanMember(Value& a_object, const char* a_name, bool& a_value);
    [[nodiscard]] bool Payload(const Params& a_params, Value& a_data);
    [[nodiscard]] std::string JoinMemberNames(Value& a_object, std::size_t a_limit);
}
