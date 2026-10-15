#include "Starfield/ShipboardCruiseBridge.h"

#include "REL/Pattern.h"
#include "REL/Relocation.h"
#include "REX/REX.h"

#include <cstddef>
#include <cstring>

namespace
{
    constexpr REL::ID SetCourseId {1016246};
    constexpr REL::ID CanActivateId {1016255};
    constexpr REL::ID StartCruiseId {1016257};
    constexpr REL::ID ReadActiveId {1016812};

    constexpr std::ptrdiff_t CourseReadInstructionOffset = 0x38;
    constexpr auto CourseReadPattern = REL::Pattern<"8B 0D ?? ?? ?? ??">();

    using SetCourseFunction = void(FormID);
    using CanActivateFunction = bool();
    using StartCruiseFunction = void(bool);
    using ReadActiveFunction = bool();

    std::uintptr_t DecodeRipTarget(std::uintptr_t instruction, std::size_t displacementOffset, std::size_t instructionSize)
    {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, reinterpret_cast<const void*>(instruction + displacementOffset), sizeof(displacement));
        return instruction + instructionSize + displacement;
    }
}

bool ShipboardCruiseBridge::Initialize()
{
    if (m_available) {
        return true;
    }

    const auto setCourseAddress = SetCourseId.address();
    const auto readInstruction = setCourseAddress + CourseReadInstructionOffset;

    // Only inspect the instruction whose displacement we actually decode.
    if (!CourseReadPattern.match(readInstruction)) {
        REX::ERROR("ShipboardCruiseBridge: course load instruction unavailable; free-roam Cruise disabled");
        return false;
    }

    const auto readTarget = DecodeRipTarget(readInstruction, 2, 6);
    if (readTarget == 0 || (readTarget % alignof(FormID)) != 0) {
        REX::ERROR("ShipboardCruiseBridge: invalid course address; free-roam Cruise disabled");
        return false;
    }

    m_currentCourseAddress = readTarget;
    m_available = true;
    REX::INFO("ShipboardCruiseBridge: Cruise state/start/course bindings enabled");
    return true;
}

bool ShipboardCruiseBridge::Available() const noexcept
{
    return m_available;
}

CruiseControlSnapshot ShipboardCruiseBridge::Read(const ShipContext& opened, const ShipContext& live) const
{
    if (!m_available || !opened.IsShipboard() || !live.IsShipboard() || !opened.SameShipAs(live)) {
        return {};
    }

    const auto state = ReadState();
    bool engageAvailable = false;
    if (state == ObservedCruiseState::Inactive && opened.CanStartCruise() && live.CanStartCruise()) {
        static REL::Relocation<CanActivateFunction> canActivate {CanActivateId};
        engageAvailable = DecideShipboardActivation(opened, live, false, canActivate(), m_available) != ShipboardActivationMode::Rejected;
    }

    return {
        .cruiseState = state,
        .engageAvailable = engageAvailable,
        .currentCourseId = ReadCurrentCourse(),
        .source = CruiseControlSource::Native,
    };
}

ObservedCruiseState ShipboardCruiseBridge::ReadState() const
{
    if (!m_available) {
        return ObservedCruiseState::Unknown;
    }

    static REL::Relocation<ReadActiveFunction> readActive {ReadActiveId};
    return readActive() ? ObservedCruiseState::Active : ObservedCruiseState::Inactive;
}

FormID ShipboardCruiseBridge::ReadCurrentCourse() const
{
    if (!m_available) {
        return 0;
    }

    FormID courseId = 0;
    std::memcpy(&courseId, reinterpret_cast<const void*>(m_currentCourseAddress), sizeof(courseId));
    return courseId;
}

bool ShipboardCruiseBridge::Start(const ShipContext& opened, const ShipContext& live, bool& usedGuardedFallback) const
{
    usedGuardedFallback = false;
    if (!m_available) {
        return false;
    }

    static REL::Relocation<CanActivateFunction> canActivate {CanActivateId};
    const auto mode = DecideShipboardActivation(opened, live, ReadState() == ObservedCruiseState::Active, canActivate(), m_available);
    if (mode == ShipboardActivationMode::Rejected) {
        return false;
    }

    usedGuardedFallback = mode == ShipboardActivationMode::GuardedFreeRoam;
    static REL::Relocation<StartCruiseFunction> startCruise {StartCruiseId};
    startCruise(usedGuardedFallback);
    REX::INFO("ShipboardCruiseBridge: requested native Cruise start path={}", usedGuardedFallback ? "guarded-free-roam" : "vanilla-eligible");
    return true;
}

bool ShipboardCruiseBridge::SetCourse(FormID courseId) const
{
    if (!m_available || courseId == 0 || ReadState() != ObservedCruiseState::Active) {
        return false;
    }

    if (ReadCurrentCourse() == courseId) {
        return true;
    }

    static REL::Relocation<SetCourseFunction> setCourse {SetCourseId};
    setCourse(courseId);
    return true;
}

bool ShipboardCruiseBridge::RefreshCourse(FormID courseId) const
{
    if (!m_available || courseId == 0 || ReadState() != ObservedCruiseState::Active) {
        return false;
    }
    if (ReadCurrentCourse() != courseId) {
        return SetCourse(courseId);
    }

    // Starfield treats an exact repeated course as the Autopilot-off toggle.
    // Clear that inherited lock first, then establish it as a fresh course.
    static REL::Relocation<SetCourseFunction> setCourse {SetCourseId};
    setCourse(0);
    if (ReadCurrentCourse() != 0) {
        return false;
    }
    setCourse(courseId);
    return true;
}
