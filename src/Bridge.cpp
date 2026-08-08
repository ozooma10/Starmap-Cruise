#include "Bridge.h"

#include "BodyIndex.h"
#include "Engine/RuntimeBindings.h"
#include "Engine/RuntimeMemory.h"
#include "Input/CruiseBindingResolver.h"
#include "MainThreadUiPump.h"
#include "Scaleform/ValueAccess.h"
#include "Settings.h"
#include "Types.h"

#include "RE/B/BSInputEventUser.h"
#include "RE/B/BSService.h"
#include "RE/U/UI.h"
#include "SFSE/SFSE.h"

#include <Windows.h>
#undef ERROR

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace CFS::Bridge
{
    namespace
    {
        using Engine::HexBytes;
        using Engine::ReadMemory;
        using ScaleformValue::AsNumber;
        using ScaleformValue::BooleanMember;
        using ScaleformValue::ObjectMember;
        using ScaleformValue::Payload;
        using ScaleformValue::StringMember;
        using ScaleformValue::UIntMember;

#include "Bridge/State.inl"
#include "Bridge/Destination.inl"
#include "Bridge/SafetyEvents.inl"
#include "Bridge/Selection.inl"
#include "Bridge/RemoteRoute/Inspection.inl"
#include "Bridge/RemoteRoute/Course.inl"
#include "Bridge/RemoteRoute/MapAction.inl"
#include "Bridge/RemoteRoute/Driver.inl"
#include "Bridge/MapUi/Input.inl"
#include "Bridge/MapUi/ActionHint.inl"
#include "Bridge/MapUi/Providers.inl"
#include "Bridge/HudCruise.inl"
#include "Bridge/Lifecycle/Subscriptions.inl"
#include "Bridge/Lifecycle/Continuation.inl"
#include "Bridge/Lifecycle/FramePump.inl"
    }

    void OnMovieCreated(RE::IMenu* a_menu)
    {
        if (!a_menu)
            return;
        const char* name = a_menu->menuName.c_str();
        if (!name)
            return;
        MovieState* state = nullptr;
        if (std::strcmp(name, kMapMenu) == 0)
            state = &g_mapMovie;
        else if (std::strcmp(name, kHudMenu) == 0)
            state = &g_hudMovie;
        else
            return;

        state->generation.fetch_add(1, std::memory_order_acq_rel);
        state->subscriptions.store(0, std::memory_order_release);
        state->bornTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
        if (state == &g_mapMovie) {
            ResetHold("Starmap movie replacement");
            g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
            g_mapActionHintSignature.store(0, std::memory_order_release);
            g_mapActionInteractive.store(false, std::memory_order_release);
            g_mapActionTapOnly.store(false, std::memory_order_release);
            g_mapUiDirty.store(true, std::memory_order_release);
            g_uiResetMask.fetch_or(kResetMapUi, std::memory_order_acq_rel);
        } else {
            ResetHold("Spaceship HUD movie replacement");
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                g_hudCruiseUserEvent = "Cruise";
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
            }
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_confirmedCourseID.store(0, std::memory_order_release);
            g_haveCurrentSystem.store(false, std::memory_order_release);
            {
                std::lock_guard lock{ g_hudRowsMutex };
                g_hudRows.clear();
                g_hudRowsGeneration = 0;
            }
            {
                std::lock_guard lock{ g_processedHudMutex };
                g_processedHudSnapshot = {};
            }
            g_hudLowDirty.store(false, std::memory_order_release);
            g_cruiseActive.store(false, std::memory_order_release);
            g_cruiseEngageAvailable.store(false, std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
            g_uiResetMask.fetch_or(kResetHudUi, std::memory_order_acq_rel);
            if (RemoteMoonContinuationActive())
                FailRemoteMoonContinuation("Spaceship HUD movie was replaced during automatic continuation");
            else if (RemoteStationContinuationActive())
                FailRemoteStationContinuation(
                    "Spaceship HUD movie was replaced during automatic continuation");
        }
        REX::INFO("[ui] movie created: {} generation={}", name,
            state->generation.load(std::memory_order_acquire));
    }

    void OnFrame()
    {
        // SFSE permanent tasks run on rotating render-graph workers. This is a
        // coalesced producer only; engine work drains through BSService on the
        // game main thread.
        if (g_mainFramePending.exchange(true, std::memory_order_acq_rel))
            return;
        if (!QueueMainThreadFrame())
            g_mainFramePending.store(false, std::memory_order_release);
    }

    void OnUiSafeFrame()
    {
        static std::atomic<bool> faulted{ false };
        static auto nextMapRoutePoll = Clock::time_point{};
        if (faulted.load(std::memory_order_acquire))
            return;

        try {
            ReleaseStaleUiState();
            TrySubscribe();
            ProcessLowSnapshot();

            const auto action = g_pendingMapAction.exchange(
                MapAction::kNone, std::memory_order_acq_rel);
            if (action != MapAction::kNone)
                AcceptSelection(action);

            DriveRemoteRouteRequest();

            const auto now = Clock::now();
            if (g_mapOpen.load(std::memory_order_acquire) &&
                now >= nextMapRoutePoll) {
                // Plot-route state is native-pushed directly into JumpData_mc
                // and is not guaranteed to publish one of our subscribed feeds.
                // Poll only in the verified post-advance window; signatures
                // keep unchanged button state mutation-free.
                nextMapRoutePoll = now + kMapRoutePollTime;
                g_mapUiDirty.store(true, std::memory_order_release);
            }

            if (g_mapUiDirty.load(std::memory_order_acquire)) {
                const auto ui = RE::UI::GetSingleton();
                const RE::BSFixedString mapName{ kMapMenu };
                if (ui && ui->IsMenuOpen(mapName)) {
                    g_mapUiDirty.store(false, std::memory_order_release);
                    UpdateMapActionHint();
                }
            }
            ReconcileHudUi();
        } catch (const std::exception& e) {
            const bool unresolvedPressedEdge = FailClosedPostAdvanceState(
                "post-advance UI pump exception");
            faulted.store(true, std::memory_order_release);
            REX::ERROR("post-advance UI pump threw '{}'; guarded navigation state cleared and further Scaleform work disabled{}",
                e.what(), unresolvedPressedEdge ?
                              "; a dispatched HUD Cruise press could not be safely released" : "");
        } catch (...) {
            const bool unresolvedPressedEdge = FailClosedPostAdvanceState(
                "unknown post-advance UI pump exception");
            faulted.store(true, std::memory_order_release);
            REX::ERROR("post-advance UI pump threw an unknown exception; guarded navigation state cleared and further Scaleform work disabled{}",
                unresolvedPressedEdge ?
                    "; a dispatched HUD Cruise press could not be safely released" : "");
        }
    }

    void Initialize()
    {
        const auto menus = SFSE::GetMenuInterface();
        const auto tasks = SFSE::GetTaskInterface();
        const auto ui = RE::UI::GetSingleton();
        if (!menus || !tasks || !ui) {
            REX::ERROR("required SFSE/UI interface unavailable (menu={} task={} ui={}); bridge disabled",
                static_cast<bool>(menus), static_cast<bool>(tasks), static_cast<bool>(ui));
            return;
        }

        if (!RuntimeBindings::Initialize())
            return;
        TryInstallLoadGameSink();
        TryInstallGravJumpSink();
        if (!MainThreadUiPump::Install()) {
            REX::ERROR("post-advance UI pump unavailable; bridge disabled to prevent off-thread Scaleform access");
            return;
        }

        ResolveCruiseMapBinding();
        BodyIndex::StartLoad();
        menus->Register(&OnMovieCreated);
        ui->RegisterSink<RE::MenuOpenCloseEvent>(&g_menuSink);
        TryInstallInputHook();
        StartFocusWatcher();
        g_lastUnsettledTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
        tasks->AddPermanentTask(&OnFrame);
        REX::INFO("bridge initialized: current-system Cruise plus guarded remote stock Back/native-selected-system/QuickSelect-SetCourse/ExecuteRoute handoff, no serialization or public API");
    }
}
