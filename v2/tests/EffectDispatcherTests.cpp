#include "Application/EffectDispatcher.h"
#include "TestSuites.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    enum class RecordedCommand
    {
        CloseMap,
        PressCruise,
        RequestCourse,
    };

    struct RecordedCall
    {
        RecordedCommand command;
        ::FormID courseId {0};
    };

    class FakeCruiseCommands final : public ::CruiseCommands
    {
    public:
        bool CloseMap() override
        {
            return Record(RecordedCommand::CloseMap);
        }

        bool PressCruise() override
        {
            return Record(RecordedCommand::PressCruise);
        }

        bool RequestCourse(::FormID courseId) override
        {
            return Record(RecordedCommand::RequestCourse, courseId);
        }

        std::optional<RecordedCommand> failOn;
        std::vector<RecordedCall> calls;

    private:
        bool Record(RecordedCommand command, ::FormID courseId = 0)
        {
            calls.push_back({
                .command = command,
                .courseId = courseId,
            });

            return !failOn || *failOn != command;
        }
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    void TestUnhandledTransitionDoesNotDispatch()
    {
        FakeCruiseCommands commands;
        ::TransitionResult transition;
        transition.effects.emplace_back(::CloseMap {});

        const auto result = ::DispatchEffects(transition, commands);

        Require(!result.handled, "unhandled transition was reported as handled");
        Require(!result.Succeeded(), "unhandled transition was reported as successful");
        Require(result.completedCount == 0, "unhandled transition completed an effect");
        Require(!result.failedEffect, "unhandled transition reported an execution failure");
        Require(commands.calls.empty(), "unhandled transition dispatched a command");
    }

    void TestHandledTransitionWithoutEffectsSucceeds()
    {
        FakeCruiseCommands commands;
        const ::TransitionResult transition {
            .handled = true,
        };

        const auto result = ::DispatchEffects(transition, commands);

        Require(result.handled, "handled transition lost its handled state");
        Require(result.Succeeded(), "handled transition without effects did not succeed");
        Require(result.completedCount == 0, "empty transition reported a completed effect");
        Require(!result.failedEffect, "empty transition reported an execution failure");
        Require(commands.calls.empty(), "empty transition dispatched a command");
    }

    void TestDispatchesAllEffectsInOrder()
    {
        FakeCruiseCommands commands;
        ::TransitionResult transition {
            .handled = true,
        };
        transition.effects.emplace_back(::CloseMap {});
        transition.effects.emplace_back(::PressCruise {});
        transition.effects.emplace_back(::RequestCourse {0x12345678});

        const auto result = ::DispatchEffects(transition, commands);

        Require(result.Succeeded(), "successful commands produced a dispatch failure");
        Require(result.completedCount == 3, "dispatcher reported the wrong completed count");
        Require(commands.calls.size() == 3, "dispatcher issued the wrong number of commands");
        Require(commands.calls[0].command == RecordedCommand::CloseMap, "CloseMap was not dispatched first");
        Require(commands.calls[1].command == RecordedCommand::PressCruise, "PressCruise was not dispatched second");
        Require(commands.calls[2].command == RecordedCommand::RequestCourse, "RequestCourse was not dispatched third");
        Require(commands.calls[2].courseId == 0x12345678, "RequestCourse received the wrong course ID");
    }

    void TestStopsOnFirstFailure()
    {
        FakeCruiseCommands commands;
        commands.failOn = RecordedCommand::PressCruise;

        ::TransitionResult transition {
            .handled = true,
        };
        transition.effects.emplace_back(::CloseMap {});
        transition.effects.emplace_back(::PressCruise {});
        transition.effects.emplace_back(::RequestCourse {0x12345678});

        const auto result = ::DispatchEffects(transition, commands);

        Require(result.handled, "failed dispatch lost the transition's handled state");
        Require(!result.Succeeded(), "failed command was reported as successful");
        Require(result.completedCount == 1, "failed command was counted as completed");
        Require(result.failedEffect.has_value(), "failed command did not retain its effect");
        Require(std::get_if<::PressCruise>(&*result.failedEffect) != nullptr, "dispatcher retained the wrong failed effect");
        Require(commands.calls.size() == 2, "dispatcher continued after the first failed command");
        Require(commands.calls[0].command == RecordedCommand::CloseMap, "successful command before failure was not issued");
        Require(commands.calls[1].command == RecordedCommand::PressCruise, "expected failing command was not issued");
    }

    void RunTests()
    {
        TestUnhandledTransitionDoesNotDispatch();
        TestHandledTransitionWithoutEffectsSucceeds();
        TestDispatchesAllEffectsInOrder();
        TestStopsOnFirstFailure();
    }
}

void RunEffectDispatcherTests()
{
    RunTests();
}
