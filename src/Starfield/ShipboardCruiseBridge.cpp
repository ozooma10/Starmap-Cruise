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
    constexpr std::ptrdiff_t CourseWriteInstructionOffset = 0x17A;

    constexpr auto SetCoursePattern = REL::Pattern<
        "48 89 5C 24 08 48 89 74 24 18 48 89 7C 24 20 55 48 8D 6C 24 D0 48 81 EC 30 01 00 00">();
    constexpr auto CourseReadPattern = REL::Pattern<"8B 0D ?? ?? ?? ?? 85 FF 0F 84">();
    constexpr auto CourseWritePattern = REL::Pattern<"89 3D ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 3B F8">();
    constexpr auto CanActivatePattern = REL::Pattern<
        "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 8B 88 7C 03 00 00">();
    constexpr auto StartCruisePattern = REL::Pattern<
        "48 8B C4 48 89 58 08 48 89 70 18 48 89 78 20 41 56 48 81 EC A0 00 00 00">();
    constexpr auto ReadActivePattern = REL::Pattern<"0F B6 05 ?? ?? ?? ?? C3">();

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
    const auto canActivateAddress = CanActivateId.address();
    const auto startCruiseAddress = StartCruiseId.address();
    const auto readActiveAddress = ReadActiveId.address();
    const auto readInstruction = setCourseAddress + CourseReadInstructionOffset;
    const auto writeInstruction = setCourseAddress + CourseWriteInstructionOffset;

    if (!SetCoursePattern.match(setCourseAddress) || !CourseReadPattern.match(readInstruction) ||
        !CourseWritePattern.match(writeInstruction) || !CanActivatePattern.match(canActivateAddress) ||
        !StartCruisePattern.match(startCruiseAddress) || !ReadActivePattern.match(readActiveAddress)) {
        REX::ERROR("ShipboardCruiseBridge: 1.16.244 native binding fingerprint failed; free-roam Cruise disabled");
        return false;
    }

    const auto readTarget = DecodeRipTarget(readInstruction, 2, 6);
    const auto writeTarget = DecodeRipTarget(writeInstruction, 2, 6);
    if (readTarget == 0 || readTarget != writeTarget || (readTarget % alignof(FormID)) != 0) {
        REX::ERROR("ShipboardCruiseBridge: course read/write target proof failed; free-roam Cruise disabled");
        return false;
    }

    m_currentCourseAddress = readTarget;
    m_available = true;
    REX::INFO("ShipboardCruiseBridge: guarded Cruise state/start/course bindings enabled");
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
    if (state == ObservedCruiseState::Unknown) {
        return {};
    }

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
    if (!m_available || m_currentCourseAddress == 0) {
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
