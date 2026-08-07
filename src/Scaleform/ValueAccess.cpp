#include "Scaleform/ValueAccess.h"

namespace CFS::ScaleformValue
{
    double AsNumber(const Value& a_value)
    {
        if (a_value.IsUInt())
            return a_value.GetUInt();
        if (a_value.IsInt())
            return a_value.GetInt();
        if (a_value.IsNumber())
            return a_value.GetNumber();
        return 0.0;
    }

    std::uint32_t UIntMember(Value& a_object, const char* a_name)
    {
        Value member;
        return a_object.GetMember(a_name, &member) ?
                   static_cast<std::uint32_t>(AsNumber(member)) :
                   0;
    }

    std::string StringMember(Value& a_object, const char* a_name)
    {
        Value member;
        if (!a_object.GetMember(a_name, &member) || !member.IsString())
            return {};
        const char* text = member.GetString();
        return text ? text : "";
    }

    bool ObjectMember(Value& a_object, const char* a_name, Value& a_member)
    {
        return a_object.GetMember(a_name, &a_member) &&
               (a_member.IsObject() || a_member.IsDisplayObject());
    }

    bool BooleanMember(Value& a_object, const char* a_name, bool& a_value)
    {
        Value member;
        if (!a_object.GetMember(a_name, &member) || !member.IsBoolean())
            return false;
        a_value = member.GetBoolean();
        return true;
    }

    bool Payload(const Params& a_params, Value& a_data)
    {
        if (!a_params.args || a_params.argCount == 0)
            return false;
        a_data = a_params.args[0];
        Value inner;
        if (a_data.IsObject() && a_data.GetMember("data", &inner))
            a_data = inner;
        return a_data.IsObject() || a_data.IsArray();
    }
}
