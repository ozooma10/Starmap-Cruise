#include "Scaleform/ValueAccess.h"

#include <format>
#include <vector>

namespace CFS::ScaleformValue
{
    namespace
    {
        class MemberNameCollector : public Value::ObjectVisitor
        {
        public:
            explicit MemberNameCollector(std::size_t a_limit) : limit(a_limit) {}

            bool IncludeAS3PublicMembers() const override { return true; }

            void Visit(const char* a_name, const Value& a_value) override
            {
                ++seen;
                if (!a_name || names.size() >= limit)
                    return;
                const char* kind = "value";
                if (a_value.IsArray())
                    kind = "array";
                else if (a_value.IsDisplayObject())
                    kind = "displayobject";
                else if (a_value.IsObject())
                    kind = "object";
                else if (a_value.IsBoolean())
                    kind = "bool";
                else if (a_value.IsString() || a_value.IsStringW())
                    kind = "string";
                else if (a_value.IsNumber() || a_value.IsInt() || a_value.IsUInt())
                    kind = "number";
                names.emplace_back(std::format("{}:{}", a_name, kind));
            }

            std::vector<std::string> names;
            std::size_t seen{ 0 };
            std::size_t limit{ 0 };
        };
    }

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

    std::string JoinMemberNames(Value& a_object, std::size_t a_limit)
    {
        if (!a_object.IsObject())
            return "<not an object>";
        MemberNameCollector collector{ a_limit };
        a_object.VisitMembers(&collector);
        std::string joined;
        for (const auto& name : collector.names) {
            if (!joined.empty())
                joined += ", ";
            joined += name;
        }
        if (collector.seen > collector.names.size())
            joined += std::format(", ...(+{} more)",
                collector.seen - collector.names.size());
        return joined.empty() ? "<none>" : joined;
    }
}
