#include "Starfield/StarfieldCruiseAdapter.h"

#include "Input/CruiseBindingResolver.h"
#include "Presentation/ActionPresenter.h"
#include "Scaleform/UiEventDispatch.h"
#include "Scaleform/ValueAccess.h"
#include "Starfield/UiPostAdvanceHook.h"

#include "RE/Starfield.h"
#include "REL/Pattern.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <Windows.h>
#undef ERROR

#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr const char* HudMenuName = "SpaceshipHudMenu";
    constexpr const char* LoadingMenuName = "LoadingMenu";
    constexpr const char* MapDataFeed = "StarMapMenuData";
    constexpr const char* MarkersFeed = "StarMapMenuMarkersData";
    constexpr const char* DossierFeed = "StarmapSystemBodyInfoProvider";
    constexpr const char* HudCourseFeed = "TargetLowFrequencyProvider";
    constexpr const char* CruiseUserEvent = "Cruise";
    constexpr const char* GamepadCruiseUserEvent = "SHMonocle";
    constexpr const char* FtlPluginName = "FTL.esm";
    constexpr auto MapMovieSettleTime = std::chrono::milliseconds(250);
    constexpr auto HudMovieSettleTime = std::chrono::milliseconds(250);
    constexpr auto HudPollInterval = std::chrono::milliseconds(50);
    constexpr auto MapCloseTimeout = std::chrono::seconds(2);
    constexpr auto CruisePressTimeout = std::chrono::seconds(4);
    constexpr auto CourseLockTimeout = std::chrono::milliseconds(1500);
    constexpr auto RemoteWorldSettleTime = std::chrono::milliseconds(2500);
    constexpr auto RemoteCruiseExitTimeout = std::chrono::seconds(2);

    constexpr auto GlobalEventGetterPattern = REL::Pattern<"48 83 EC 28 65 48 8B 04 25 58 00 00 00 BA B8 00">();

    constexpr std::uint32_t PlanetType = 2;
    constexpr std::uint32_t MoonType = 3;
    constexpr std::uint32_t StationType = 4;

    std::uintptr_t PackIdentity(const MapSessionIdentity& identity)
    {
        static_assert(sizeof(std::uintptr_t) >= sizeof(std::uint64_t));
        return (static_cast<std::uintptr_t>(identity.session) << 32) | identity.generation;
    }

    MapSessionIdentity UnpackIdentity(const void* token)
    {
        const auto packed = reinterpret_cast<std::uintptr_t>(token);
        return {
            .session = static_cast<std::uint32_t>(packed >> 32),
            .generation = static_cast<std::uint32_t>(packed),
        };
    }

    bool IsPlayerFlying()
    {
        static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);

        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto ship = player ? player->GetSpaceship() : nullptr;
        return ship && ship->IsInSpace(false);
    }

    template <class Files> bool ContainsPlugin(const Files& files, const char* name)
    {
        for (const auto* file : files) {
            if (file && std::strcmp(file->fileName, name) == 0) {
                return true;
            }
        }
        return false;
    }

    bool IsPluginLoaded(const char* name)
    {
        const auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return false;
        }

        const auto& files = data->compiledFileCollection;
        return ContainsPlugin(files.files, name) || ContainsPlugin(files.smallFiles, name) || ContainsPlugin(files.mediumFiles, name);
    }

    template <class Event> RE::BSTEventSource<Event>* ProveEventSource()
    {
        const auto& binding = Event::EVENT_SOURCE_BINDING;
        if (!GlobalEventGetterPattern.match(binding.GetGetterID().address())) {
            return nullptr;
        }

        auto* source = binding.Get();
        return binding.Matches(source) ? source : nullptr;
    }

    ObservedCruiseState ReadCruiseState()
    {
        const auto ui = RE::UI::GetSingleton();
        const RE::BSFixedString hudName {HudMenuName};
        if (!ui || !ui->IsMenuOpen(hudName)) {
            return ObservedCruiseState::Unknown;
        }

        const auto menu = ui->GetMenu(hudName);
        if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
            return ObservedCruiseState::Unknown;
        }

        auto* root = menu->uiMovie->asMovieRoot.get();
        const char* rootPath = menu->GetRootPath();
        const std::string reticlePath = std::string {rootPath ? rootPath : "root"} + ".Reticle_mc";

        RE::Scaleform::GFx::Value reticle;
        RE::Scaleform::GFx::Value cruiseActive;
        if (!root->GetVariable(&reticle, reticlePath.c_str()) || !reticle.IsObject() || !reticle.GetMember("CruiseModeHUDActive", &cruiseActive) || !cruiseActive.IsBoolean()) {
            return ObservedCruiseState::Unknown;
        }

        const bool active = cruiseActive.GetBoolean();

        const auto currentMenu = ui->GetMenu(hudName);
        if (!ui->IsMenuOpen(hudName) || !currentMenu || !currentMenu->uiMovie || !currentMenu->uiMovie->asMovieRoot || currentMenu->uiMovie->asMovieRoot.get() != root) {
            return ObservedCruiseState::Unknown;
        }

        return active ? ObservedCruiseState::Active : ObservedCruiseState::Inactive;
    }

    MapView ReadMapView(RE::Scaleform::GFx::Value& data)
    {
        RE::Scaleform::GFx::Value value;
        if (!data.GetMember("iCurrentMenuView", &value) || !(value.IsNumber() || value.IsInt() || value.IsUInt())) {
            return MapView::Unknown;
        }

        const auto raw = CFS::ScaleformValue::AsNumber(value);
        if (raw == 0.0) {
            return MapView::Galaxy;
        }
        if (raw == 1.0) {
            return MapView::System;
        }
        return MapView::Other;
    }

    ObservedTargetKind ReadTargetKind(std::uint32_t raw)
    {
        if (raw == PlanetType) {
            return ObservedTargetKind::Planet;
        }
        if (raw == MoonType) {
            return ObservedTargetKind::Moon;
        }
        if (raw == StationType) {
            return ObservedTargetKind::Station;
        }
        return ObservedTargetKind::Unsupported;
    }

    const char* SelectionAvailabilityName(SelectionAvailability availability)
    {
        switch (availability) {
        case SelectionAvailability::Hidden:
            return "hidden";
        case SelectionAvailability::Disabled:
            return "disabled";
        case SelectionAvailability::Eligible:
            return "eligible";
        }

        return "unknown";
    }

    const char* CruiseStateName(ObservedCruiseState state)
    {
        switch (state) {
        case ObservedCruiseState::Unknown:
            return "unknown";
        case ObservedCruiseState::Inactive:
            return "inactive";
        case ObservedCruiseState::Active:
            return "active";
        }

        return "unknown";
    }

    const char* SelectionReasonName(SelectionReason reason)
    {
        switch (reason) {
        case SelectionReason::InactiveContext:
            return "inactive-context";
        case SelectionReason::CurrentSystemUnavailable:
            return "current-system-unavailable";
        case SelectionReason::SelectDestination:
            return "select-destination";
        case SelectionReason::AmbiguousTarget:
            return "ambiguous-target";
        case SelectionReason::UnsupportedTarget:
            return "unsupported-target";
        case SelectionReason::TargetDataUpdating:
            return "target-data-updating";
        case SelectionReason::TargetSystemUnavailable:
            return "target-system-unavailable";
        case SelectionReason::RemoteSystem:
            return "remote-system";
        case SelectionReason::Eligible:
            return "eligible";
        }

        return "unknown";
    }

    class MarkerCollector final : public RE::Scaleform::GFx::Value::ArrayVisitor
    {
    public:
        void Visit(std::uint32_t, const RE::Scaleform::GFx::Value& value) override
        {
            auto entry = value;
            bool highlighted = false;
            if (!CFS::ScaleformValue::BooleanMember(entry, "bIsInHighlightRadius", highlighted) || !highlighted) {
                return;
            }

            highlightedCount++;
            highlightedTarget = {
                .id = CFS::ScaleformValue::UIntMember(entry, "uBodyID"),
                .kind = ReadTargetKind(CFS::ScaleformValue::UIntMember(entry, "uBodyType")),
                .displayName = CFS::ScaleformValue::StringMember(entry, "sMarkerText"),
            };
        }

        std::size_t highlightedCount {0};
        TargetObservation highlightedTarget;
    };

    class LockedCourseCollector final : public RE::Scaleform::GFx::Value::ArrayVisitor
    {
    public:
        void Visit(std::uint32_t, const RE::Scaleform::GFx::Value& value) override
        {
            auto entry = value;
            const auto id = CFS::ScaleformValue::UIntMember(entry, "uniqueID");
            if (id != 0) {
                if (rowCount < rows.size()) {
                    rows[rowCount++] = id;
                } else {
                    overflowed = true;
                }
            }

            bool locked = false;
            if (!CFS::ScaleformValue::BooleanMember(entry, "bIsCruiseTargetLock", locked) || !locked) {
                return;
            }

            lockedCount++;
            courseId = id;
        }

        std::size_t lockedCount {0};
        FormID courseId {0};
        std::array<FormID, HudObservationInbox::MaxCourseRows> rows;
        std::size_t rowCount {0};
        bool overflowed {false};
    };
}

class StarfieldCruiseAdapter::MapLifecycleSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    explicit MapLifecycleSink(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        const char* name = event.menuName.c_str();
        if (name && std::strcmp(name, MapMenuName) == 0) {
            m_owner.m_mapObservations.RecordLifecycle(event.opening);
        } else if (name && std::strcmp(name, LoadingMenuName) == 0) {
            m_owner.m_travelObservations.RecordLoadingMenu(event.opening);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::GravJumpSink final : public RE::BSTEventSink<RE::Spaceship::GravJumpEvent>
{
public:
    explicit GravJumpSink(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    RE::BSEventNotifyControl ProcessEvent(const RE::Spaceship::GravJumpEvent& event, RE::BSTEventSource<RE::Spaceship::GravJumpEvent>*) override
    {
        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && event.ship && event.ship.get() == player->GetSpaceship()) {
                m_owner.m_travelObservations.RecordGravJump(event.state, event.destination ? event.destination->GetFormID() : 0);
            }
        } catch (...) {}
        return RE::BSEventNotifyControl::kContinue;
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::LoadGameSink final : public RE::BSTEventSink<RE::TESLoadGameEvent>
{
public:
    explicit LoadGameSink(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
    {
        m_owner.m_travelObservations.RecordLoadGame();
        return RE::BSEventNotifyControl::kContinue;
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::MapDataHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit MapDataHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        MapView view = MapView::Unknown;
        FormID currentSystemFormId = 0;

        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            view = ReadMapView(data);
            currentSystemFormId = CFS::ScaleformValue::UIntMember(data, "uSystemLocationID");
        }

        m_owner.m_mapObservations.RecordMapData(identity, view, currentSystemFormId);
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::MarkersHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit MarkersHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        MarkerUpdate update;
        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            RE::Scaleform::GFx::Value markers;
            if (data.GetMember("aMarkersData", &markers)) {
                RE::Scaleform::GFx::Value inner;
                if (markers.GetMember("dataA", &inner) && inner.IsArray()) {
                    markers = inner;
                }

                if (markers.IsArray()) {
                    MarkerCollector collector;
                    markers.VisitElements(&collector);
                    update.highlightedCount = collector.highlightedCount;
                    if (collector.highlightedCount == 1) {
                        update.highlighted = std::move(collector.highlightedTarget);
                    }
                }
            }
        }

        m_owner.m_mapObservations.RecordMarkers(identity, std::move(update));
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::DossierHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit DossierHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        TargetObservation target;
        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            target = {
                .id = CFS::ScaleformValue::UIntMember(data, "uBodyID"),
                .kind = ReadTargetKind(CFS::ScaleformValue::UIntMember(data, "iType")),
                .displayName = CFS::ScaleformValue::StringMember(data, "sBodyName"),
            };
        }

        m_owner.m_mapObservations.RecordDossier(identity, std::move(target));
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::HudCourseHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit HudCourseHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto generation = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(params.userData));

        FormID courseId = 0;
        std::array<FormID, HudObservationInbox::MaxCourseRows> rows;
        std::size_t rowCount = 0;
        bool overflowed = false;
        bool valid = false;
        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            RE::Scaleform::GFx::Value targets;
            if (data.GetMember("targetArray", &targets)) {
                RE::Scaleform::GFx::Value inner;
                if (targets.GetMember("dataA", &inner) && inner.IsArray()) {
                    targets = inner;
                }

                if (targets.IsArray()) {
                    valid = true;
                    LockedCourseCollector collector;
                    targets.VisitElements(&collector);
                    if (collector.lockedCount == 1) {
                        courseId = collector.courseId;
                    }
                    rows = collector.rows;
                    rowCount = collector.rowCount;
                    overflowed = collector.overflowed;
                }
            }
        }

        if (valid) {
            m_owner.m_hudObservations.RecordCourse(generation, courseId, rows, rowCount, overflowed);
        }
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::MapActionSurface final : public ::MapActionView
{
    using Value = RE::Scaleform::GFx::Value;

    class Handler final : public RE::Scaleform::GFx::FunctionHandler
    {
    public:
        Handler(StarfieldCruiseAdapter& owner, MapObservationInbox::Action action) : m_owner(owner), m_action(action) {}

        void Call(const Params& params) override
        {
            const auto identity = m_owner.m_activeMapIdentity;
            const auto* root = params.movie && params.movie->asMovieRoot ? params.movie->asMovieRoot.get() : nullptr;
            if (!m_owner.IsCurrentMapMovie(root, identity)) {
                return;
            }

            m_owner.m_mapObservations.RecordAction(identity, m_action);
        }

    private:
        StarfieldCruiseAdapter& m_owner;
        MapObservationInbox::Action m_action;
    };

public:
    explicit MapActionSurface(StarfieldCruiseAdapter& owner) : m_owner(owner), m_tapHandler(owner, MapObservationInbox::Action::Tap), m_holdHandler(owner, MapObservationInbox::Action::HoldCompleted) {}

    void Present(const ActionDecision& decision)
    {
        m_presenter.Present(decision, *this);
    }

    void InvalidateMovie()
    {
        m_presenter.Invalidate();
        m_generation = 0;
        m_comboReady = false;
        m_tapReady = false;
        m_comboButton = Value {};
        m_comboKeyboardData = Value {};
        m_comboGamepadData = Value {};
        m_tapButton = Value {};
        m_tapKeyboardData = Value {};
        m_tapGamepadData = Value {};
        m_owner.m_mapActionInteractive.store(false, std::memory_order_release);
    }

    void InvalidatePresentation()
    {
        m_presenter.Invalidate();
    }

    bool Apply(const MapActionPresentation& presentation) override
    {
        RE::Scaleform::GFx::ASMovieRootBase* root = nullptr;
        Value vanillaData;
        Value buttonBar;
        if (!Resolve(root, vanillaData, buttonBar)) {
            m_owner.m_mapActionInteractive.store(false, std::memory_order_release);
            return false;
        }

        const auto generation = m_owner.m_activeMapIdentity.generation;
        if (m_generation != 0 && m_generation != generation) {
            InvalidateMovie();
        }
        m_generation = generation;

        if (presentation.control == ActionControl::TapOnly && !m_tapReady && !BuildButton(root, buttonBar, vanillaData, false)) {
            return false;
        }
        if (presentation.control == ActionControl::TapAndHold && !m_comboReady && !BuildButton(root, buttonBar, vanillaData, true)) {
            return false;
        }

        const bool showTap = presentation.control == ActionControl::TapOnly && m_tapReady;
        const bool showCombo = presentation.control == ActionControl::TapAndHold && m_comboReady;
        const bool gamepad = m_owner.m_presentedInputWasGamepad;

        bool tapApplied = true;
        bool comboApplied = true;
        if (m_tapReady) {
            tapApplied = UpdateButton(m_tapButton, m_tapKeyboardData, m_tapGamepadData, presentation, showTap, false, gamepad);
        }
        if (m_comboReady) {
            comboApplied = UpdateButton(m_comboButton, m_comboKeyboardData, m_comboGamepadData, presentation, showCombo, true, gamepad);
        }

        buttonBar.Invoke("RefreshButtons");

        const bool desiredApplied = presentation.control == ActionControl::Hidden || (showTap && tapApplied) || (showCombo && comboApplied);
        m_owner.m_mapActionInteractive.store(desiredApplied && presentation.enabled, std::memory_order_release);
        return desiredApplied;
    }

private:
    bool Resolve(RE::Scaleform::GFx::ASMovieRootBase*& root, Value& vanillaData, Value& buttonBar)
    {
        const auto identity = m_owner.m_activeMapIdentity;
        if (!identity.IsValid()) {
            return false;
        }

        const auto ui = RE::UI::GetSingleton();
        const RE::BSFixedString mapName {MapMenuName};
        const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
        if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
            return false;
        }

        root = menu->uiMovie->asMovieRoot.get();
        if (!m_owner.IsCurrentMapMovie(root, identity)) {
            return false;
        }

        const char* path = menu->GetRootPath();
        Value menuRoot;
        Value hintBar;
        if (!root->GetVariable(&menuRoot, path && *path ? path : "root") || !(menuRoot.IsObject() || menuRoot.IsDisplayObject()) || !menuRoot.GetMember("ButtonHintBar_mc", &hintBar) || !(hintBar.IsObject() || hintBar.IsDisplayObject()) ||
            !hintBar.GetMember("SetRouteDestinationButtonData", &vanillaData) || !(vanillaData.IsObject() || vanillaData.IsDisplayObject()) || !hintBar.GetMember("HintBar_mc", &buttonBar) ||
            !(buttonBar.IsObject() || buttonBar.IsDisplayObject())) {
            return false;
        }

        return m_owner.IsCurrentMapMovie(root, identity);
    }

    bool BuildButton(RE::Scaleform::GFx::ASMovieRootBase* root, Value& buttonBar, Value& vanillaData, bool combo)
    {
        Value tapCallback;
        root->CreateFunction(&tapCallback, &m_tapHandler);

        Value holdCallback;
        if (combo) {
            root->CreateFunction(&holdCallback, &m_holdHandler);
        }

        Value keyboardData;
        Value gamepadData;
        if (!BuildData(root, vanillaData, tapCallback, holdCallback, CruiseUserEvent, combo, keyboardData) || !BuildData(root, vanillaData, tapCallback, holdCallback, GamepadCruiseUserEvent, combo, gamepadData)) {
            return false;
        }

        Value factory;
        if (!root->GetVariable(&factory, "Shared.Components.ButtonControls.ButtonFactory.ButtonFactory") || !(factory.IsObject() || factory.IsDisplayObject())) {
            return false;
        }

        Value buttonType;
        root->CreateString(&buttonType, combo ? "ReleaseHoldComboButton" : "BasicButton");
        Value& initialData = m_owner.m_presentedInputWasGamepad ? gamepadData : keyboardData;
        Value args[3] {buttonType, initialData, buttonBar};
        Value button;
        if (!factory.Invoke("AddToButtonBar", &button, args, 3) || !(button.IsObject() || button.IsDisplayObject())) {
            return false;
        }

        if (combo) {
            m_comboReady = true;
            m_comboButton = std::move(button);
            m_comboKeyboardData = std::move(keyboardData);
            m_comboGamepadData = std::move(gamepadData);
        } else {
            m_tapReady = true;
            m_tapButton = std::move(button);
            m_tapKeyboardData = std::move(keyboardData);
            m_tapGamepadData = std::move(gamepadData);
        }
        REX::INFO("StarfieldCruiseAdapter: installed {} map action control generation={}", combo ? "tap/hold" : "tap-only", m_generation);
        return true;
    }

    bool BuildData(RE::Scaleform::GFx::ASMovieRootBase* root, Value& vanillaData, Value& tapCallback, Value& holdCallback, const char* userEvent, bool combo, Value& data)
    {
        Value eventName;
        root->CreateString(&eventName, userEvent);
        Value tapArgs[2] {eventName, tapCallback};
        Value tapEvent;
        root->CreateObject(&tapEvent, "Shared.Components.ButtonControls.ButtonData.UserEventData", tapArgs, 2);
        if (!(tapEvent.IsObject() || tapEvent.IsDisplayObject())) {
            return false;
        }

        Value events;
        root->CreateArray(&events);
        if (!events.IsArray() || !events.PushBack(tapEvent)) {
            return false;
        }

        if (combo) {
            Value emptyName;
            root->CreateString(&emptyName, "");
            Value holdArgs[2] {emptyName, holdCallback};
            Value holdEvent;
            root->CreateObject(&holdEvent, "Shared.Components.ButtonControls.ButtonData.UserEventData", holdArgs, 2);
            if (!(holdEvent.IsObject() || holdEvent.IsDisplayObject()) || !events.PushBack(holdEvent)) {
                return false;
            }
        }

        Value label;
        root->CreateString(&label, "SET CRUISE TARGET");
        if (combo) {
            Value holdLabel;
            root->CreateString(&holdLabel, "HOLD TO CRUISE");
            Value args[3] {label, holdLabel, events};
            root->CreateObject(&data, "Shared.Components.ButtonControls.ButtonData.ReleaseHoldComboButtonData", args, 3);
        } else {
            Value args[2] {label, events};
            root->CreateObject(&data, "Shared.Components.ButtonControls.ButtonData.ButtonBaseData", args, 2);
        }
        if (!(data.IsObject() || data.IsDisplayObject())) {
            return false;
        }

        for (const char* member : {"bEnabled", "bVisible"}) {
            Value value;
            if (vanillaData.GetMember(member, &value)) {
                data.SetMember(member, value);
            }
        }
        return true;
    }

    bool UpdateButton(Value& button, Value& keyboardData, Value& gamepadData, const MapActionPresentation& presentation, bool visible, bool combo, bool gamepad)
    {
        for (Value* data : {&keyboardData, &gamepadData}) {
            data->SetMember("bEnabled", Value {presentation.enabled && visible});
            data->SetMember("bVisible", Value {visible});
            data->SetMember("sButtonText", Value {presentation.label.c_str()});
            if (combo) {
                data->SetMember("sHoldText", Value {presentation.enabled && visible ? presentation.holdLabel.c_str() : ""});
            }
        }

        Value& activeData = gamepad ? gamepadData : keyboardData;
        const bool applied = button.Invoke("SetButtonData", nullptr, &activeData, 1);
        button.Invoke("RefreshButtonData");
        return applied;
    }

    StarfieldCruiseAdapter& m_owner;
    ::ActionPresenter m_presenter;
    Handler m_tapHandler;
    Handler m_holdHandler;

    std::uint32_t m_generation {0};
    bool m_comboReady {false};
    bool m_tapReady {false};
    Value m_comboButton;
    Value m_comboKeyboardData;
    Value m_comboGamepadData;
    Value m_tapButton;
    Value m_tapKeyboardData;
    Value m_tapGamepadData;
};

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter()
    : m_commands(*this), m_runtime(m_bodySource, m_commands), m_mapActionSurface(std::make_unique<MapActionSurface>(*this)), m_mapLifecycleSink(std::make_unique<MapLifecycleSink>(*this)),
      m_mapDataHandler(std::make_unique<MapDataHandler>(*this)), m_markersHandler(std::make_unique<MarkersHandler>(*this)), m_dossierHandler(std::make_unique<DossierHandler>(*this)),
      m_hudCourseHandler(std::make_unique<HudCourseHandler>(*this)), m_gravJumpSink(std::make_unique<GravJumpSink>(*this)), m_loadGameSink(std::make_unique<LoadGameSink>(*this))
{}

StarfieldCruiseAdapter::~StarfieldCruiseAdapter() = default;

bool StarfieldCruiseAdapter::Initialize()
{
    if (m_initialized) {
        return true;
    }
    if (m_initializationAttempted) {
        REX::ERROR("StarfieldCruiseAdapter: prior initialization attempt failed after process-lifetime hook setup began; retry refused");
        return false;
    }

    const auto menus = SFSE::GetMenuInterface();
    const auto ui = RE::UI::GetSingleton();
    if (!menus || !ui) {
        REX::ERROR("StarfieldCruiseAdapter: required menu interface unavailable (sfse={} ui={}); plugin disabled", static_cast<bool>(menus), static_cast<bool>(ui));
        return false;
    }

    m_initializationAttempted = true;
    if (!CFS::UiPostAdvanceHook::Install(&OnUiSafeFrame)) {
        REX::ERROR("StarfieldCruiseAdapter: Scaleform post-advance pump unavailable; plugin disabled");
        return false;
    }

    ResolveInputBindings();
    if (!InstallInput()) {
        REX::ERROR("StarfieldCruiseAdapter: UI input hook unavailable; plugin disabled");
        return false;
    }

    menus->Register(&OnMovieCreated);
    ui->RegisterSink<RE::MenuOpenCloseEvent>(m_mapLifecycleSink.get());
    const bool planetaryIdentityReady = m_bodySource.InitializeRemotePlanning();
    const bool routeReady = planetaryIdentityReady && m_remoteRoute.Initialize(m_bodySource);
    const bool travelReady = InitializeTravelObservers();
    m_ftlReplacementJumpLoaded = IsPluginLoaded(FtlPluginName);
    m_remoteRoutingAvailable = planetaryIdentityReady && routeReady && travelReady;
    if (!m_remoteRoutingAvailable) {
        REX::WARN("StarfieldCruiseAdapter: remote planet/moon routing disabled because one or more native guards failed; same-system Cruise remains enabled");
    }
    if (m_ftlReplacementJumpLoaded) {
        REX::INFO("StarfieldCruiseAdapter: FTL replacement-jump compatibility enabled");
    }
    m_initialized = true;
    REX::INFO("StarfieldCruiseAdapter: initialized with map action presentation, copied observations, and typed Starfield effects");
    return true;
}

bool StarfieldCruiseAdapter::InitializeTravelObservers()
{
    auto* gravSource = ProveEventSource<RE::Spaceship::GravJumpEvent>();
    auto* loadSource = ProveEventSource<RE::TESLoadGameEvent>();
    if (!gravSource || !loadSource) {
        REX::ERROR("StarfieldCruiseAdapter: guarded GravJump/TESLoadGame sources unavailable; remote routing disabled");
        return false;
    }
    gravSource->RegisterSink(m_gravJumpSink.get());
    loadSource->RegisterSink(m_loadGameSink.get());
    REX::INFO("StarfieldCruiseAdapter: guarded player GravJump and TESLoadGame observers enabled");
    return true;
}

void StarfieldCruiseAdapter::OnMovieCreated(RE::IMenu* menu)
{
    if (!menu) {
        return;
    }

    const char* name = menu->menuName.c_str();
    if (!name) {
        return;
    }

    auto& adapter = GetSingleton();
    const auto bornTicks = Clock::now().time_since_epoch().count();
    if (std::strcmp(name, MapMenuName) == 0) {
        adapter.m_mapObservations.RecordMovieCreated(bornTicks);
    } else if (std::strcmp(name, HudMenuName) == 0) {
        adapter.m_hudObservations.RecordMovieCreated(bornTicks);
    }
}

void StarfieldCruiseAdapter::OnUiSafeFrame()
{
    static bool faulted = false;
    if (faulted) {
        return;
    }

    auto& adapter = GetSingleton();
    if (!adapter.m_initialized) {
        return;
    }
    try {
        adapter.RefreshInputPresentation();
        adapter.DrainHudObservations();
        adapter.DrainTravelObservations();
        adapter.DrainMapObservations();
        adapter.m_mapActionInput.ExpireReleased();
        adapter.TrySubscribeMapFeeds();
        adapter.TrySubscribeHudFeed();
        adapter.UpdateHudRuntime();
        adapter.HandleRemoteRouteResult(adapter.m_remoteRoute.Tick(adapter.m_activeMapIdentity));
        adapter.EvaluateRemoteArrival();
        adapter.UpdateTimeouts();
        adapter.UpdateMapAction();
    } catch (const std::exception& error) {
        adapter.m_mapActionInteractive.store(false, std::memory_order_release);
        adapter.ResetMapActionInput();
        const bool unresolvedPress = adapter.m_hudCruisePressed;
        if (adapter.m_hudCruisePressed) {
            try {
                if (adapter.InvokeHudCruiseUserEvent(adapter.m_hudCruiseUserEvent.c_str(), false)) {
                    adapter.m_hudCruisePressed = false;
                }
            } catch (...) {}
        }
        faulted = true;
        REX::ERROR(
            "StarfieldCruiseAdapter: post-advance action boundary threw '{}'; further UI work disabled{}",
            error.what(),
            unresolvedPress && adapter.m_hudCruisePressed ? "; a dispatched HUD Cruise press could not be safely released" : ""
        );
    } catch (...) {
        adapter.m_mapActionInteractive.store(false, std::memory_order_release);
        adapter.ResetMapActionInput();
        const bool unresolvedPress = adapter.m_hudCruisePressed;
        if (adapter.m_hudCruisePressed) {
            try {
                if (adapter.InvokeHudCruiseUserEvent(adapter.m_hudCruiseUserEvent.c_str(), false)) {
                    adapter.m_hudCruisePressed = false;
                }
            } catch (...) {}
        }
        faulted = true;
        REX::ERROR(
            "StarfieldCruiseAdapter: post-advance action boundary threw an unknown exception; further UI work disabled{}",
            unresolvedPress && adapter.m_hudCruisePressed ? "; a dispatched HUD Cruise press could not be safely released" : ""
        );
    }
}

void StarfieldCruiseAdapter::DrainMapObservations()
{
    auto observations = m_mapObservations.Drain();

    if (observations.movieCreated) {
        HandleRemoteRouteResult(m_remoteRoute.OnMovieCreated(observations.movieGeneration));
        m_stationTargets.Invalidate();
        m_runtime.OnMapMovieCreated(observations.movieGeneration);
        m_mapActionSurface->InvalidateMovie();
        ResetMapActionInput();
        m_mapMovieBornTicks = observations.movieBornTicks;
        m_activeMapIdentity = {};
        m_mapDataSubscriptionIdentity = {};
        m_markersSubscriptionIdentity = {};
        m_dossierSubscriptionIdentity = {};
    }

    if (observations.lifecycleOverflowed) {
        const bool hadRemoteOperation = m_runtime.CurrentNavigationState().remoteOperation.has_value();
        m_remoteRoute.Cancel();
        if (hadRemoteOperation) {
            m_runtime.OnLoadGame();
            ResetRemoteTravelState();
        }
        m_stationTargets.Invalidate();
        if (!observations.movieCreated && observations.movieGeneration != 0) {
            m_runtime.OnMapMovieCreated(observations.movieGeneration);
        }
        m_activeMapIdentity = {};
        m_mapDataSubscriptionIdentity = {};
        m_markersSubscriptionIdentity = {};
        m_dossierSubscriptionIdentity = {};
        ResetMapActionInput();
        REX::ERROR("StarfieldCruiseAdapter: map lifecycle observation queue overflowed; active map session invalidated");
        TraceCurrentSelection();
        return;
    }

    for (std::size_t index = 0; index < observations.lifecycleCount; ++index) {
        const auto& observation = observations.lifecycle[index];

        if (observation.opening) {
            const bool flying = IsPlayerFlying();
            const auto cruiseState = ReadCruiseState();
            const auto currentSystem = m_bodySource.ResolveCurrentSystem();
            const bool accepted = m_runtime.OnMapOpened({
                .identity = observation.identity,
                .flying = flying,
                .cruiseState = cruiseState,
                .currentSystemId = currentSystem ? std::optional<FormID> {currentSystem->numericId} : std::nullopt,
            });

            if (accepted && currentSystem) {
                m_runtime.OnCurrentSystemFormObserved(observation.identity, currentSystem->starFormId);
            }

            REX::INFO(
                "StarfieldCruiseAdapter: map open session={} generation={} flying={} cruise-state={} current-STDT={:08X} current-numeric={:08X} accepted={}",
                observation.identity.session,
                observation.identity.generation,
                flying,
                CruiseStateName(cruiseState),
                currentSystem ? currentSystem->starFormId : 0,
                currentSystem ? currentSystem->numericId : 0,
                accepted
            );

            if (accepted) {
                m_activeMapIdentity = observation.identity;
                m_mapActionSurface->InvalidatePresentation();
            }
        } else {
            m_pendingMapCloseIdentity = {};
            m_mapCloseStarted = {};
            const bool activeSession = m_activeMapIdentity == observation.identity;
            const auto routeResult = m_remoteRoute.OnMapClosed(observation.identity);
            const auto closed = m_runtime.OnMapClosed(observation.identity);
            HandleRemoteRouteResult(routeResult);
            m_stationTargets.Invalidate();
            if (closed.targetAssignmentFailed) {
                REX::ERROR("StarfieldCruiseAdapter: station target assignment failed after map close; selection discarded");
            } else if (closed.failedEffect) {
                REX::ERROR("StarfieldCruiseAdapter: map close transition failed to dispatch its next effect");
            }
            if (activeSession) {
                m_activeMapIdentity = {};
                m_mapActionInteractive.store(false, std::memory_order_release);
            }
        }
    }

    if (observations.mapData && observations.mapData->identity == m_activeMapIdentity) {
        const auto& mapData = *observations.mapData;

        // RemoteRouteProtocol compares an STDT system root. The displayed
        // uBodyLocationID is a PNDT and is intentionally not retained.
        m_remoteRoute.ObserveMapData(mapData.identity, mapData.view, mapData.currentSystemFormId);

        // MapData describes the system being browsed, which can be remote.
        // Selection policy needs the player's map-independent current pair.
        if (const auto currentSystem = m_bodySource.ResolveCurrentSystem()) {
            m_runtime.OnCurrentSystemResolved(mapData.identity, currentSystem->numericId);
            m_runtime.OnCurrentSystemFormObserved(mapData.identity, currentSystem->starFormId);
        }

        m_runtime.OnMapViewChanged(mapData.identity, mapData.view);
    }

    if (observations.markers && observations.markers->identity == m_activeMapIdentity) {
        m_stationTargets.Resolve(observations.markers->update.highlighted);
        m_runtime.OnMarkersChanged(observations.markers->identity, std::move(observations.markers->update));
    }

    if (observations.dossier && observations.dossier->identity == m_activeMapIdentity) {
        m_runtime.OnDossierChanged(observations.dossier->identity, observations.dossier->target);
    }

    if (observations.actionOverflowed) {
        REX::ERROR("StarfieldCruiseAdapter: multiple map actions arrived in one UI frame; all were rejected");
    } else if (observations.action && observations.action->identity == m_activeMapIdentity) {
        auto gesture = MapActionGesture::Tap;
        auto environment = ReadMapActionEnvironment();
        const auto selection = m_runtime.CurrentSelection();
        const bool holdRequested = observations.action->action == MapObservationInbox::Action::HoldCompleted;
        const auto inputDevice = m_mapActionInput.AcceptAction();
        if (inputDevice) {
            environment.inputDevice = *inputDevice == static_cast<std::uint32_t>(RE::InputEvent::DeviceType::kGamepad) ? NavigationInputDevice::Gamepad : NavigationInputDevice::KeyboardMouse;
        }
        if (holdRequested) {
            gesture = MapActionGesture::HoldCompleted;
            m_pendingCruiseInputDevice = inputDevice;
        } else {
            m_pendingCruiseInputDevice.reset();
        }
        if (holdRequested && !inputDevice) {
            REX::INFO("StarfieldCruiseAdapter: completed map hold outlived physical input tracking; using the active input presentation");
        }
        const auto previousRemoteOperationId = m_runtime.CurrentNavigationState().remoteOperation ? m_runtime.CurrentNavigationState().remoteOperation->id : 0;
        if (previousRemoteOperationId != 0) {
            // The user's new action owns replacement/toggle. Drop any old
            // concrete route or HUD dispatch before the core emits a new one.
            m_remoteRoute.Cancel();
            ClearRemoteDispatchState(previousRemoteOperationId);
        }
        const auto activated = m_runtime.ActivateMapAction(observations.action->identity, gesture, environment);
        if (previousRemoteOperationId != 0 && !activated.handled) {
            m_runtime.OnRemoteOperationCancelled(previousRemoteOperationId);
        }
        if (activated.handled) {
            m_mapActionInteractive.store(false, std::memory_order_release);
            REX::INFO(
                "StarfieldCruiseAdapter: accepted map {} target={:08X} name='{}'",
                gesture == MapActionGesture::HoldCompleted ? "hold" : "tap",
                selection.destination ? selection.destination->targetId : 0,
                selection.destination ? selection.destination->displayName : ""
            );
        }
        if (!activated.handled) {
            REX::WARN("StarfieldCruiseAdapter: current map action was rejected by the runtime");
        } else if (activated.failedEffect) {
            REX::ERROR("StarfieldCruiseAdapter: current map action failed while dispatching its first effect");
            m_mapActionSurface->InvalidatePresentation();
        }
        if (!activated.Succeeded()) {
            ResetMapActionInput();
        }
    }

    TraceCurrentSelection();
}

void StarfieldCruiseAdapter::TraceCurrentSelection()
{
    const auto selection = m_runtime.CurrentSelection();

    SelectionTrace trace {
        .identity = m_activeMapIdentity,
        .availability = selection.availability,
        .reason = selection.reason,
    };

    if (selection.destination) {
        trace.targetId = selection.destination->targetId;
        trace.system = selection.destination->system;
        trace.displayName = selection.destination->displayName;
    }

    if (m_lastSelectionTrace && *m_lastSelectionTrace == trace) {
        return;
    }

    m_lastSelectionTrace = trace;

    if (trace.targetId != 0 && trace.system) {
        REX::INFO(
            "StarfieldCruiseAdapter: selection session={} generation={} availability={} reason={} target={:08X} STDT={:08X} numeric={:08X} name='{}'",
            trace.identity.session,
            trace.identity.generation,
            SelectionAvailabilityName(trace.availability),
            SelectionReasonName(trace.reason),
            trace.targetId,
            trace.system->starFormId,
            trace.system->numericId,
            trace.displayName
        );
        return;
    }

    REX::INFO(
        "StarfieldCruiseAdapter: selection session={} generation={} availability={} reason={} target=none system=none name=''",
        trace.identity.session,
        trace.identity.generation,
        SelectionAvailabilityName(trace.availability),
        SelectionReasonName(trace.reason)
    );
}

bool StarfieldCruiseAdapter::IsCurrentMapMovie(const void* root, const MapSessionIdentity& identity)
{
    if (!root || identity != m_activeMapIdentity) {
        return false;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString mapName {MapMenuName};
    if (!ui || !ui->IsMenuOpen(mapName)) {
        return false;
    }

    const auto menu = ui->GetMenu(mapName);
    return menu && menu->uiMovie && menu->uiMovie->asMovieRoot && static_cast<const void*>(menu->uiMovie->asMovieRoot.get()) == root;
}

void StarfieldCruiseAdapter::TrySubscribeMapFeeds()
{
    const auto identity = m_activeMapIdentity;
    if (!identity.IsValid()) {
        return;
    }

    const char* feed = nullptr;
    RE::Scaleform::GFx::FunctionHandler* handler = nullptr;
    MapSessionIdentity* subscriptionIdentity = nullptr;

    if (m_mapDataSubscriptionIdentity != identity) {
        feed = MapDataFeed;
        handler = m_mapDataHandler.get();
        subscriptionIdentity = &m_mapDataSubscriptionIdentity;
    } else if (m_markersSubscriptionIdentity != identity) {
        feed = MarkersFeed;
        handler = m_markersHandler.get();
        subscriptionIdentity = &m_markersSubscriptionIdentity;
    } else if (m_dossierSubscriptionIdentity != identity) {
        feed = DossierFeed;
        handler = m_dossierHandler.get();
        subscriptionIdentity = &m_dossierSubscriptionIdentity;
    } else {
        return;
    }

    if (m_mapMovieBornTicks == 0 || Clock::now() - Clock::time_point {Clock::duration {m_mapMovieBornTicks}} < MapMovieSettleTime) {
        return;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString mapName {MapMenuName};
    if (!ui || !ui->IsMenuOpen(mapName)) {
        return;
    }

    const auto menu = ui->GetMenu(mapName);
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return;
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    const auto rootIdentity = static_cast<const void*>(root);
    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    RE::Scaleform::GFx::Value manager;
    if (!root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") || !(manager.IsObject() || manager.IsDisplayObject()) || !IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    RE::Scaleform::GFx::Value args[2];
    root->CreateString(&args[0], feed);
    root->CreateFunction(&args[1], handler, reinterpret_cast<void*>(PackIdentity(identity)));

    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    if (!manager.Invoke("Subscribe", nullptr, args, 2)) {
        return;
    }

    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    *subscriptionIdentity = identity;
    REX::INFO("StarfieldCruiseAdapter: subscribed {} -> {} session={} generation={}", MapMenuName, feed, identity.session, identity.generation);
}

void StarfieldCruiseAdapter::DrainHudObservations()
{
    const auto observations = m_hudObservations.Drain();

    if (observations.movie) {
        OperationId cancelledRemoteOperation {0};
        m_hudMovieGeneration = observations.movie->generation;
        m_hudMovieBornTicks = observations.movie->bornTicks;
        m_hudSubscriptionGeneration = 0;
        m_lastCruiseActive.reset();
        m_hudSnapshot = {};
        m_lastHudCourse.reset();
        m_nextHudPoll = {};
        m_mapActionSurface->InvalidatePresentation();

        if (m_hudCruisePressed) {
            const auto operationId = m_hudCruiseOperationId;
            m_hudCruisePressed = false;
            m_hudCruiseTimedOut = false;
            m_hudCruiseOperationId = 0;
            m_hudCruiseUserEvent.clear();
            m_hudCruiseStarted = {};
            m_runtime.OnCruiseActivationTimedOut(operationId);
            if (operationId != 0) {
                cancelledRemoteOperation = operationId;
            }
        }
        if (m_pendingCourseId != 0) {
            const auto courseId = std::exchange(m_pendingCourseId, 0);
            const auto operationId = std::exchange(m_pendingCourseOperationId, 0);
            m_courseRequestStarted = {};
            if (operationId != 0) {
                m_runtime.OnRemoteOperationCancelled(operationId);
                cancelledRemoteOperation = operationId;
            } else {
                m_runtime.OnCourseLockTimedOut(courseId);
            }
        }
        if (cancelledRemoteOperation != 0) {
            ResetRemoteTravelState();
        }
    }

    if (!observations.course) {
        return;
    }

    const auto generation = observations.course->generation;
    if (generation == 0 || generation != m_hudMovieGeneration) {
        return;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString hudName {HudMenuName};
    const auto menu = ui ? ui->GetMenu(hudName) : nullptr;
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot || !IsCurrentHudMovie(menu->uiMovie->asMovieRoot.get(), generation)) {
        return;
    }

    const auto courseId = observations.course->courseId;
    m_lastHudCourse = observations.course;
    const auto remoteOperationBefore = m_runtime.CurrentNavigationState().remoteOperation ? m_runtime.CurrentNavigationState().remoteOperation->id : 0;
    const bool exactPendingCourse = courseId != 0 && courseId == m_pendingCourseId;
    m_runtime.OnCourseLockChanged(courseId);
    if (exactPendingCourse) {
        m_pendingCourseId = 0;
        m_pendingCourseOperationId = 0;
        m_courseRequestStarted = {};
        REX::INFO("StarfieldCruiseAdapter: HUD confirmed exact course {:08X}", courseId);
    }

    if (remoteOperationBefore != 0 && !m_runtime.CurrentNavigationState().remoteOperation) {
        ClearRemoteDispatchState(remoteOperationBefore);
    }
}

void StarfieldCruiseAdapter::DrainTravelObservations()
{
    const auto observations = m_travelObservations.Drain();
    if (observations.overflowed) {
        m_remoteRoute.Cancel();
        m_runtime.OnLoadGame();
        if (m_hudCruisePressed && InvokeHudCruiseUserEvent(m_hudCruiseUserEvent.c_str(), false)) {
            m_hudCruisePressed = false;
        }
        m_hudCruiseOperationId = 0;
        m_pendingCourseId = 0;
        m_pendingCourseOperationId = 0;
        m_courseRequestStarted = {};
        ResetRemoteTravelState();
        REX::ERROR("StarfieldCruiseAdapter: travel observation queue overflowed; navigation reset");
        return;
    }

    for (std::size_t index = 0; index < observations.count; ++index) {
        const auto& observation = observations.values[index];
        if (observation.kind == TravelObservationInbox::Kind::LoadGame) {
            m_remoteRoute.Cancel();
            m_runtime.OnLoadGame();
            m_stationTargets.Invalidate();
            m_pendingMapCloseIdentity = {};
            m_mapCloseStarted = {};
            m_pendingCourseId = 0;
            m_pendingCourseOperationId = 0;
            m_courseRequestStarted = {};
            if (m_hudCruisePressed && InvokeHudCruiseUserEvent(m_hudCruiseUserEvent.c_str(), false)) {
                m_hudCruisePressed = false;
            }
            m_hudCruiseTimedOut = false;
            m_hudCruiseOperationId = 0;
            m_hudCruiseUserEvent.clear();
            m_hudCruiseStarted = {};
            ResetRemoteTravelState();
            REX::INFO("StarfieldCruiseAdapter: TESLoadGameEvent performed one complete navigation reset");
            continue;
        }

        m_lastTravelTicks = observation.ticks;
        m_lastRemoteUnsettled = Clock::time_point {Clock::duration {observation.ticks}};
        if (observation.kind == TravelObservationInbox::Kind::LoadingMenu) {
            m_loadingMenuOpen = observation.opening;
            continue;
        }

        m_playerJumpState.Observe(observation.gravState);
        REX::INFO(
            "StarfieldCruiseAdapter: player grav-jump state={} destination={:08X} started={} completed={}",
            observation.gravState,
            observation.destinationId,
            m_playerJumpState.Started(),
            m_playerJumpState.Completed()
        );
    }
}

void StarfieldCruiseAdapter::EvaluateRemoteArrival()
{
    const auto& navigation = m_runtime.CurrentNavigationState();
    if (!navigation.remoteOperation || navigation.phase != NavigationPhase::PendingRemoteArrival) {
        m_invalidFlightSince = {};
        return;
    }

    const auto now = Clock::now();
    const bool flying = IsPlayerFlying();

    const auto hud = ReadHudSnapshot();
    const bool cruiseCanEngage = hud.cruiseState == ObservedCruiseState::Inactive && hud.engageAvailable;
    const bool completedReplacementJump = m_ftlReplacementJumpLoaded && m_playerJumpState.Started() && !m_playerJumpState.Completed() && cruiseCanEngage;
    const bool completedPlayerTravel = m_playerJumpState.Completed() || completedReplacementJump;
    if (m_activeMapIdentity.IsValid() || m_loadingMenuOpen || !completedPlayerTravel || !cruiseCanEngage || !flying || m_lastRemoteUnsettled == Clock::time_point {} || now - m_lastRemoteUnsettled < RemoteWorldSettleTime || !m_lastHudCourse ||
        m_lastHudCourse->generation != m_hudMovieGeneration || m_lastHudCourse->publishedTicks <= m_lastTravelTicks) {
        return;
    }

    const auto currentSystem = m_bodySource.ResolveCurrentSystem();
    if (!currentSystem) {
        return;
    }

    const auto operationId = navigation.remoteOperation->id;
    RemoteArrivalObservation observation {
        .operationId = operationId,
        .currentSystem = *currentSystem,
        .mapClosed = true,
        .loadingMenuClosed = true,
        .completedPlayerJump = m_playerJumpState.Completed(),
        .completedReplacementJump = completedReplacementJump,
        .settledFlight = true,
        .flying = true,
        .freshHudPublication = true,
        .courseRowsComplete = !m_lastHudCourse->overflowed,
    };
    observation.courseRows.reserve(m_lastHudCourse->rowCount);
    for (std::size_t index = 0; index < m_lastHudCourse->rowCount; ++index) {
        observation.courseRows.push_back(m_lastHudCourse->rows[index]);
    }

    const bool courseRowsComplete = observation.courseRowsComplete;
    const auto result = m_runtime.OnRemoteArrival(std::move(observation));
    if (result.failedEffect) {
        ClearRemoteDispatchState(operationId);
        REX::ERROR("StarfieldCruiseAdapter: final-system remote activation failed to dispatch");
    } else if (result.handled) {
        if (m_runtime.CurrentNavigationState().phase == NavigationPhase::PreparingRemoteTarget) {
            REX::INFO(
                "StarfieldCruiseAdapter: operation={} accepted exact final-system arrival via {} and dispatched one stock Cruise press",
                operationId,
                completedReplacementJump ? "FTL replacement completion" : "vanilla grav-jump completion"
            );
        } else if (!courseRowsComplete) {
            REX::WARN("StarfieldCruiseAdapter: operation={} cancelled after proven final-system arrival because the HUD course row snapshot exceeded its fixed bound", operationId);
        } else {
            REX::WARN("StarfieldCruiseAdapter: operation={} cancelled by ambiguous final-system HUD rows", operationId);
        }
    }
}

void StarfieldCruiseAdapter::HandleRemoteRouteResult(RemoteRouteResult result)
{
    if (!result) {
        return;
    }
    EffectDispatchResult transition;
    if (result.kind == RemoteRouteResult::Kind::Committed) {
        transition = m_runtime.OnRemoteRouteCommitted(result.operationId, result.source);
        if (transition.handled) {
            ResetRemoteTravelState();
            m_lastRemoteUnsettled = Clock::now();
            m_lastTravelTicks = m_lastRemoteUnsettled.time_since_epoch().count();
            REX::INFO("StarfieldCruiseAdapter: operation={} committed; vanilla owns travel until exact final-system arrival", result.operationId);
        }
    } else {
        transition = m_runtime.OnRemoteRouteFailed(result.operationId, result.source);
        if (transition.handled) {
            ResetRemoteTravelState();
        }
    }
    if (!transition.handled) {
        REX::WARN("StarfieldCruiseAdapter: ignored stale remote route callback operation={} session={} generation={}", result.operationId, result.source.session, result.source.generation);
    }
}

void StarfieldCruiseAdapter::ClearRemoteDispatchState(OperationId operationId)
{
    if (operationId == 0) {
        return;
    }

    if (m_hudCruisePressed && m_hudCruiseOperationId == operationId) {
        if (InvokeHudCruiseUserEvent(m_hudCruiseUserEvent.c_str(), false)) {
            m_hudCruisePressed = false;
            m_hudCruiseTimedOut = false;
            m_hudCruiseOperationId = 0;
            m_hudCruiseUserEvent.clear();
            m_hudCruiseStarted = {};
        } else {
            // Preserve enough state for UpdateTimeouts to retry the release.
            m_hudCruiseTimedOut = true;
        }
    }
    if (m_pendingCourseOperationId == operationId) {
        m_pendingCourseId = 0;
        m_pendingCourseOperationId = 0;
        m_courseRequestStarted = {};
    }
    ResetRemoteTravelState();
}

void StarfieldCruiseAdapter::ResetRemoteTravelState()
{
    m_loadingMenuOpen = false;
    m_playerJumpState.Reset();
    m_lastTravelTicks = 0;
    m_lastRemoteUnsettled = {};
    m_invalidFlightSince = {};
    m_remoteCruiseInactiveSince = {};
    m_lastHudCourse.reset();
}

void StarfieldCruiseAdapter::TrySubscribeHudFeed()
{
    const auto generation = m_hudMovieGeneration;
    if (generation == 0 || m_hudSubscriptionGeneration == generation) {
        return;
    }

    const auto bornTicks = m_hudMovieBornTicks;
    if (bornTicks == 0 || Clock::now() - Clock::time_point {Clock::duration {bornTicks}} < HudMovieSettleTime) {
        return;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString hudName {HudMenuName};
    if (!ui || !ui->IsMenuOpen(hudName)) {
        return;
    }

    const auto menu = ui->GetMenu(hudName);
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return;
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    const auto rootIdentity = static_cast<const void*>(root);
    if (!IsCurrentHudMovie(rootIdentity, generation)) {
        return;
    }

    RE::Scaleform::GFx::Value manager;
    if (!root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") || !(manager.IsObject() || manager.IsDisplayObject()) || !IsCurrentHudMovie(rootIdentity, generation)) {
        return;
    }

    RE::Scaleform::GFx::Value args[2];
    root->CreateString(&args[0], HudCourseFeed);
    root->CreateFunction(&args[1], m_hudCourseHandler.get(), reinterpret_cast<void*>(static_cast<std::uintptr_t>(generation)));
    if (!IsCurrentHudMovie(rootIdentity, generation) || !manager.Invoke("Subscribe", nullptr, args, 2) || !IsCurrentHudMovie(rootIdentity, generation)) {
        return;
    }

    m_hudSubscriptionGeneration = generation;
    REX::INFO("StarfieldCruiseAdapter: subscribed {} -> {} generation={}", HudMenuName, HudCourseFeed, generation);
}

bool StarfieldCruiseAdapter::IsCurrentHudMovie(const void* root, std::uint32_t generation)
{
    if (!root || generation == 0 || generation != m_hudMovieGeneration || !m_hudObservations.IsCurrentGeneration(generation)) {
        return false;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString hudName {HudMenuName};
    if (!ui || !ui->IsMenuOpen(hudName)) {
        return false;
    }

    const auto menu = ui->GetMenu(hudName);
    return menu && menu->uiMovie && menu->uiMovie->asMovieRoot && static_cast<const void*>(menu->uiMovie->asMovieRoot.get()) == root;
}

StarfieldCruiseAdapter::HudSnapshot StarfieldCruiseAdapter::ReadHudSnapshot()
{
    const auto generation = m_hudMovieGeneration;
    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString hudName {HudMenuName};
    if (!ui || !ui->IsMenuOpen(hudName)) {
        return {};
    }

    const auto menu = ui->GetMenu(hudName);
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return {};
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    if (!IsCurrentHudMovie(root, generation)) {
        return {};
    }

    const char* rootPath = menu->GetRootPath();
    const std::string reticlePath = std::string {rootPath && *rootPath ? rootPath : "root"} + ".Reticle_mc";
    RE::Scaleform::GFx::Value reticle;
    if (!root->GetVariable(&reticle, reticlePath.c_str()) || !reticle.IsObject()) {
        return {};
    }

    bool active = false;
    bool canActivate = false;
    bool monocleActive = false;
    if (!CFS::ScaleformValue::BooleanMember(reticle, "CruiseModeHUDActive", active) || !IsCurrentHudMovie(root, generation)) {
        return {};
    }

    const bool engageResolved = CFS::ScaleformValue::BooleanMember(reticle, "CanActivateCruiseMode", canActivate) && CFS::ScaleformValue::BooleanMember(reticle, "MonocleModeActive", monocleActive) && IsCurrentHudMovie(root, generation);

    return {
        .cruiseState = active ? ObservedCruiseState::Active : ObservedCruiseState::Inactive,
        .engageAvailable = engageResolved && canActivate && !monocleActive && !active,
    };
}

void StarfieldCruiseAdapter::UpdateHudRuntime()
{
    const auto now = Clock::now();
    const bool navigationActive = m_runtime.CurrentNavigationState().phase != NavigationPhase::Idle;
    if (!m_activeMapIdentity.IsValid() && !navigationActive && !m_hudCruisePressed && m_pendingCourseId == 0) {
        return;
    }
    if (m_nextHudPoll != Clock::time_point {} && now < m_nextHudPoll) {
        return;
    }
    m_nextHudPoll = now + HudPollInterval;

    m_hudSnapshot = ReadHudSnapshot();
    if (m_hudSnapshot.cruiseState == ObservedCruiseState::Unknown) {
        return;
    }

    const bool active = m_hudSnapshot.cruiseState == ObservedCruiseState::Active;
    if (m_hudCruisePressed && active && InvokeHudCruiseUserEvent(m_hudCruiseUserEvent.c_str(), false)) {
        m_hudCruisePressed = false;
        m_hudCruiseTimedOut = false;
        m_hudCruiseOperationId = 0;
        m_hudCruiseUserEvent.clear();
        m_hudCruiseStarted = {};
    }

    if (!m_lastCruiseActive || *m_lastCruiseActive != active) {
        m_lastCruiseActive = active;
        const auto remoteOperationBefore = m_runtime.CurrentNavigationState().remoteOperation ? m_runtime.CurrentNavigationState().remoteOperation->id : 0;
        const auto changed = m_runtime.OnCruiseChanged(active);
        if (changed.failedEffect) {
            if (remoteOperationBefore != 0) {
                ClearRemoteDispatchState(remoteOperationBefore);
            }
            REX::ERROR("StarfieldCruiseAdapter: Cruise state transition failed to dispatch its next effect");
        }
        m_mapActionSurface->InvalidatePresentation();
    }

    const auto& navigation = m_runtime.CurrentNavigationState();
    if (!active && navigation.remoteOperation && navigation.phase == NavigationPhase::AwaitingCourseLock) {
        if (m_remoteCruiseInactiveSince == Clock::time_point {}) {
            m_remoteCruiseInactiveSince = now;
        } else if (now - m_remoteCruiseInactiveSince >= RemoteCruiseExitTimeout) {
            const auto operationId = navigation.remoteOperation->id;
            if (m_runtime.OnRemoteCruiseExitTimedOut(operationId)) {
                ClearRemoteDispatchState(operationId);
                REX::WARN("StarfieldCruiseAdapter: sustained manual Cruise exit cancelled remote operation={}", operationId);
            }
            m_remoteCruiseInactiveSince = {};
        }
    } else {
        m_remoteCruiseInactiveSince = {};
    }
}

void StarfieldCruiseAdapter::RefreshInputPresentation()
{
    const bool gamepad = m_lastInputWasGamepad.load(std::memory_order_relaxed);
    if (m_presentedInputWasGamepad == gamepad) {
        return;
    }

    m_presentedInputWasGamepad = gamepad;
    m_mapActionSurface->InvalidatePresentation();
}

MapActionEnvironment StarfieldCruiseAdapter::ReadMapActionEnvironment()
{
    const bool controlBound = m_presentedInputWasGamepad ? m_inputBindings.gamepad >= 0 : m_inputBindings.keyboard >= 0 || m_inputBindings.mouse >= 0;

    bool vanillaActionEnabled = false;
    const auto identity = m_activeMapIdentity;
    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString mapName {MapMenuName};
    const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
    if (menu && menu->uiMovie && menu->uiMovie->asMovieRoot) {
        auto* root = menu->uiMovie->asMovieRoot.get();
        const char* path = menu->GetRootPath();
        RE::Scaleform::GFx::Value menuRoot;
        RE::Scaleform::GFx::Value hintBar;
        RE::Scaleform::GFx::Value vanillaData;
        bool enabled = false;
        bool visible = false;
        if (IsCurrentMapMovie(root, identity) && root->GetVariable(&menuRoot, path && *path ? path : "root") && menuRoot.GetMember("ButtonHintBar_mc", &hintBar) && hintBar.GetMember("SetRouteDestinationButtonData", &vanillaData) &&
            CFS::ScaleformValue::BooleanMember(vanillaData, "bEnabled", enabled) && CFS::ScaleformValue::BooleanMember(vanillaData, "bVisible", visible) && IsCurrentMapMovie(root, identity)) {
            vanillaActionEnabled = enabled && visible;
        }
    }

    return {
        .cruiseControlBound = controlBound,
        .cruiseEngageAvailable = m_hudSnapshot.engageAvailable,
        .vanillaActionEnabled = vanillaActionEnabled,
        .remoteRoutingAvailable = m_remoteRoutingAvailable,
        .inputDevice = m_presentedInputWasGamepad ? NavigationInputDevice::Gamepad : NavigationInputDevice::KeyboardMouse,
    };
}

void StarfieldCruiseAdapter::UpdateMapAction()
{
    if (!m_activeMapIdentity.IsValid()) {
        m_mapActionInteractive.store(false, std::memory_order_release);
        return;
    }

    m_mapActionSurface->Present(m_runtime.CurrentMapAction(ReadMapActionEnvironment()));
}

void StarfieldCruiseAdapter::UpdateTimeouts()
{
    const auto now = Clock::now();

    const auto& navigation = m_runtime.CurrentNavigationState();
    if (navigation.remoteOperation && navigation.phase != NavigationPhase::RoutingRemote && !m_loadingMenuOpen && !IsPlayerFlying()) {
        if (m_invalidFlightSince == Clock::time_point {}) {
            m_invalidFlightSince = now;
        } else if (now - m_invalidFlightSince >= RemoteWorldSettleTime) {
            const auto operationId = navigation.remoteOperation->id;
            if (m_runtime.OnRemoteFlightInvalidated()) {
                ClearRemoteDispatchState(operationId);
                REX::WARN("StarfieldCruiseAdapter: remote operation={} cancelled after settled landing, docking, or pilot exit", operationId);
            }
        }
    } else {
        m_invalidFlightSince = {};
    }

    if (m_pendingMapCloseIdentity.IsValid() && m_mapCloseStarted != Clock::time_point {} && now - m_mapCloseStarted > MapCloseTimeout) {
        const auto identity = std::exchange(m_pendingMapCloseIdentity, {});
        m_mapCloseStarted = {};
        ResetMapActionInput();
        m_stationTargets.CancelAssignment();
        if (m_runtime.OnMapCloseTimedOut(identity)) {
            REX::WARN("StarfieldCruiseAdapter: map close timed out; incomplete selection discarded");
            m_mapActionSurface->InvalidatePresentation();
        }
    }

    if (m_hudCruisePressed && !m_hudCruiseTimedOut && m_hudCruiseStarted != Clock::time_point {} && now - m_hudCruiseStarted > CruisePressTimeout) {
        const auto operationId = m_hudCruiseOperationId;
        m_hudCruiseTimedOut = true;
        const bool recovered = m_runtime.OnCruiseActivationTimedOut(operationId);
        if (operationId != 0 && recovered) {
            ClearRemoteDispatchState(operationId);
            REX::WARN("StarfieldCruiseAdapter: remote Cruise activation timed out; operation={} cancelled", operationId);
        } else {
            REX::WARN("StarfieldCruiseAdapter: Cruise activation timed out; retained destination as a mark");
        }
    }

    if (m_hudCruisePressed && m_hudCruiseTimedOut && InvokeHudCruiseUserEvent(m_hudCruiseUserEvent.c_str(), false)) {
        m_hudCruisePressed = false;
        m_hudCruiseTimedOut = false;
        m_hudCruiseOperationId = 0;
        m_hudCruiseUserEvent.clear();
        m_hudCruiseStarted = {};
    }

    if (m_pendingCourseId != 0 && m_pendingCourseOperationId == 0 && m_courseRequestStarted != Clock::time_point {} && now - m_courseRequestStarted > CourseLockTimeout) {
        const auto courseId = std::exchange(m_pendingCourseId, 0);
        m_courseRequestStarted = {};
        if (m_runtime.OnCourseLockTimedOut(courseId)) {
            REX::WARN("StarfieldCruiseAdapter: course {:08X} did not lock in time; destination retained as a mark", courseId);
        }
    }
}

void StarfieldCruiseAdapter::ResolveInputBindings()
{
    const auto bindings = CFS::Input::ResolveCruiseBindings();
    m_inputBindings = {
        .keyboard = bindings.keyboard.code,
        .keyboardModifier = bindings.keyboard.modifier,
        .mouse = bindings.mouse.modifier < 0 ? bindings.mouse.code : -1,
        .gamepad = bindings.gamepad.modifier < 0 ? bindings.gamepad.code : -1,
    };

    REX::INFO("StarfieldCruiseAdapter: Cruise bindings keyboard={} modifier={} mouse={} gamepad={}", m_inputBindings.keyboard, m_inputBindings.keyboardModifier, m_inputBindings.mouse, m_inputBindings.gamepad);
}

bool StarfieldCruiseAdapter::InstallInput()
{
    const auto ui = RE::UI::GetSingleton();
    if (!ui) {
        return false;
    }

    auto* receiver = static_cast<RE::BSInputEventReceiver*>(ui);
    const auto vtable = *reinterpret_cast<std::uintptr_t*>(receiver);
    if (!vtable) {
        return false;
    }
    const auto original = *reinterpret_cast<std::uintptr_t*>(vtable + sizeof(void*));
    if (!original || original == reinterpret_cast<std::uintptr_t>(&ProcessInput)) {
        return false;
    }

    m_originalInput.store(reinterpret_cast<ProcessInputFunction>(original), std::memory_order_release);
    REL::Relocation<std::uintptr_t> relocation {vtable};
    relocation.write_vfunc(1, &ProcessInput);
    REX::INFO("StarfieldCruiseAdapter: UI input hook installed for the live Cruise binding");
    return true;
}

void StarfieldCruiseAdapter::ProcessInput(RE::BSInputEventReceiver* receiver, const RE::InputEvent* head)
{
    GetSingleton().ProcessInputEvents(receiver, head);
}

void StarfieldCruiseAdapter::ProcessInputEvents(RE::BSInputEventReceiver* receiver, const RE::InputEvent* head)
{
    const auto originalInput = m_originalInput.load(std::memory_order_acquire);

    if (!m_initialized) {
        if (originalInput) {
            originalInput(receiver, head);
        }
        return;
    }

    struct Fix
    {
        RE::InputEvent* node {nullptr};
        RE::InputEvent* next {nullptr};
    };

    struct RoutedEvent
    {
        RE::ButtonEvent* event {nullptr};
        RE::BSFixedString originalName;
    };

    std::array<Fix, 16> fixes;
    std::array<RoutedEvent, 16> routedEvents;
    std::size_t fixCount = 0;
    std::size_t routedCount = 0;
    const RE::InputEvent* routedHead = head;
    RE::InputEvent* previous = nullptr;

    for (auto* event = head; event;) {
        auto* next = event->next;
        bool drop = false;
        if (event->eventType != RE::InputEvent::EventType::kButton) {
            previous = const_cast<RE::InputEvent*>(event);
            event = next;
            continue;
        }

        auto* button = const_cast<RE::ButtonEvent*>(static_cast<const RE::ButtonEvent*>(event));
        const bool down = button->value != 0.0f;
        const bool first = down && button->heldDownSecs == 0.0f;
        const auto device = static_cast<std::uint32_t>(button->deviceType);

        if (first && (button->deviceType == RE::InputEvent::DeviceType::kKeyboard || button->deviceType == RE::InputEvent::DeviceType::kMouse || button->deviceType == RE::InputEvent::DeviceType::kGamepad)) {
            const bool gamepad = button->deviceType == RE::InputEvent::DeviceType::kGamepad;
            m_lastInputWasGamepad.store(gamepad, std::memory_order_relaxed);
        }

        drop = m_mapActionInput.Filter(device, button->idCode, down);

        if (!drop && m_mapActionInteractive.load(std::memory_order_acquire) && !button->disabled && routedCount < routedEvents.size()) {
            std::int32_t binding = -1;
            std::int32_t modifier = -1;
            const char* userEvent = CruiseUserEvent;
            switch (button->deviceType) {
            case RE::InputEvent::DeviceType::kKeyboard:
                binding = m_inputBindings.keyboard;
                modifier = m_inputBindings.keyboardModifier;
                break;
            case RE::InputEvent::DeviceType::kMouse:
                binding = m_inputBindings.mouse;
                break;
            case RE::InputEvent::DeviceType::kGamepad:
                binding = m_inputBindings.gamepad;
                userEvent = GamepadCruiseUserEvent;
                break;
            default:
                break;
            }

            const bool modifierReady = button->deviceType != RE::InputEvent::DeviceType::kKeyboard || !down || modifier < 0 || (::GetAsyncKeyState(modifier) & 0x8000) != 0;
            if (binding >= 0 && button->idCode == binding && modifierReady) {
                if (down) {
                    m_mapActionInput.Begin(device, button->idCode);
                }
                routedEvents[routedCount++] = {
                    .event = button,
                    .originalName = button->strUserEvent,
                };
                button->strUserEvent = RE::BSFixedString {userEvent};
            }
        }

        if (drop && fixCount < fixes.size()) {
            if (previous) {
                fixes[fixCount++] = {previous, previous->next};
                previous->next = next;
            } else {
                routedHead = next;
            }
        } else {
            previous = const_cast<RE::InputEvent*>(event);
        }
        event = next;
    }

    if (originalInput) {
        originalInput(receiver, routedHead);
    }

    for (std::size_t index = fixCount; index-- > 0;) {
        fixes[index].node->next = fixes[index].next;
    }

    for (std::size_t index = routedCount; index-- > 0;) {
        routedEvents[index].event->strUserEvent = routedEvents[index].originalName;
    }
}

void StarfieldCruiseAdapter::ResetMapActionInput()
{
    m_mapActionInput.Reset();
    m_pendingCruiseInputDevice.reset();
}

bool StarfieldCruiseAdapter::DispatchMapClose()
{
    const auto identity = m_activeMapIdentity;
    if (!identity.IsValid()) {
        return false;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString mapName {MapMenuName};
    const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return false;
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    if (!IsCurrentMapMovie(root, identity)) {
        return false;
    }

    const bool quickEntrySet = CFS::ScaleformEvents::DispatchUiEvent(root, "DataMenu_SetMenuForQuickEntry", nullptr);
    if (!quickEntrySet) {
        REX::WARN("StarfieldCruiseAdapter: stock DataMenu quick-entry event failed before close-all");
    }
    if (!IsCurrentMapMovie(root, identity) || !CFS::ScaleformEvents::DispatchUiEvent(root, "GlobalFunc_CloseAllMenus", nullptr)) {
        return false;
    }

    m_pendingMapCloseIdentity = identity;
    m_mapCloseStarted = Clock::now();
    REX::INFO("StarfieldCruiseAdapter: dispatched stock map close session={} generation={}", identity.session, identity.generation);
    return true;
}

bool StarfieldCruiseAdapter::InvokeHudCruiseUserEvent(const char* userEvent, bool down)
{
    if (!userEvent || !*userEvent) {
        return false;
    }

    const auto generation = m_hudMovieGeneration;
    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString hudName {HudMenuName};
    const auto menu = ui ? ui->GetMenu(hudName) : nullptr;
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return false;
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    if (!IsCurrentHudMovie(root, generation)) {
        return false;
    }

    RE::Scaleform::GFx::Value menuRoot;
    const char* rootPath = menu->GetRootPath();
    if (!root->GetVariable(&menuRoot, rootPath && *rootPath ? rootPath : "root") || !(menuRoot.IsObject() || menuRoot.IsDisplayObject()) || !IsCurrentHudMovie(root, generation)) {
        return false;
    }

    RE::Scaleform::GFx::Value eventName;
    root->CreateString(&eventName, userEvent);
    RE::Scaleform::GFx::Value args[2] {eventName, RE::Scaleform::GFx::Value {down}};
    RE::Scaleform::GFx::Value handled;
    const bool invoked = menuRoot.Invoke("ProcessUserEvent", &handled, args, 2);
    REX::INFO("StarfieldCruiseAdapter: forwarded stock HUD '{}' {} invoked={} handled={}", userEvent, down ? "press" : "release", invoked, handled.IsBoolean() ? handled.GetBoolean() : false);
    return invoked;
}

bool StarfieldCruiseAdapter::DispatchCourse(FormID courseId, OperationId operationId)
{
    if (courseId == 0 || ReadHudSnapshot().cruiseState != ObservedCruiseState::Active) {
        return false;
    }

    const auto generation = m_hudMovieGeneration;
    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString hudName {HudMenuName};
    const auto menu = ui ? ui->GetMenu(hudName) : nullptr;
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return false;
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    if (!IsCurrentHudMovie(root, generation)) {
        return false;
    }

    RE::Scaleform::GFx::Value params;
    root->CreateObject(&params);
    if (!params.IsObject() || !params.SetMember("uBodyID", RE::Scaleform::GFx::Value {static_cast<double>(courseId)}) || !IsCurrentHudMovie(root, generation) ||
        !CFS::ScaleformEvents::DispatchUiEvent(root, "Reticle_OnCruiseLockCourse", &params)) {
        return false;
    }

    m_pendingCourseId = courseId;
    m_pendingCourseOperationId = operationId;
    m_courseRequestStarted = Clock::now();
    REX::INFO("StarfieldCruiseAdapter: dispatched course request uBodyID={:08X} operation={}", courseId, operationId);
    return true;
}

StarfieldCruiseAdapter::Commands::Commands(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

bool StarfieldCruiseAdapter::Commands::CloseMap()
{
    const auto& navigation = m_owner.m_runtime.CurrentNavigationState();
    const bool stationAssignment = navigation.destination && navigation.destination->kind == DestinationKind::Station;
    if (stationAssignment && !m_owner.m_stationTargets.PrepareAssignment(navigation.destination->targetId)) {
        REX::ERROR("StarfieldCruiseAdapter: selected station REFR was not retained for map-close assignment");
        return false;
    }

    if (m_owner.DispatchMapClose()) {
        return true;
    }

    if (stationAssignment) {
        m_owner.m_stationTargets.Invalidate();
    }
    return false;
}

bool StarfieldCruiseAdapter::Commands::BeginRemoteRoute(const ::BeginRemoteRoute& effect)
{
    return m_owner.m_remoteRoutingAvailable && m_owner.m_remoteRoute.Begin(effect, m_owner.m_activeMapIdentity);
}

bool StarfieldCruiseAdapter::Commands::AssignStationTarget(FormID targetId)
{
    return m_owner.m_stationTargets.Assign(targetId);
}

bool StarfieldCruiseAdapter::Commands::PressCruise(OperationId operationId)
{
    const auto hud = m_owner.ReadHudSnapshot();
    if (m_owner.m_hudCruisePressed || hud.cruiseState != ObservedCruiseState::Inactive || !hud.engageAvailable) {
        return false;
    }

    bool gamepad = m_owner.m_presentedInputWasGamepad;
    if (operationId != 0) {
        const auto& navigation = m_owner.m_runtime.CurrentNavigationState();
        if (!navigation.remoteOperation || navigation.remoteOperation->id != operationId) {
            return false;
        }
        gamepad = navigation.remoteOperation->inputDevice == NavigationInputDevice::Gamepad;
    } else if (const auto device = std::exchange(m_owner.m_pendingCruiseInputDevice, std::nullopt)) {
        gamepad = *device == static_cast<std::uint32_t>(RE::InputEvent::DeviceType::kGamepad);
    }
    m_owner.m_hudCruiseUserEvent = gamepad ? GamepadCruiseUserEvent : CruiseUserEvent;
    m_owner.m_hudCruisePressed = true;
    m_owner.m_hudCruiseTimedOut = false;
    m_owner.m_hudCruiseOperationId = operationId;
    m_owner.m_hudCruiseStarted = Clock::now();
    if (!m_owner.InvokeHudCruiseUserEvent(m_owner.m_hudCruiseUserEvent.c_str(), true)) {
        m_owner.m_hudCruisePressed = false;
        m_owner.m_hudCruiseOperationId = 0;
        m_owner.m_hudCruiseUserEvent.clear();
        m_owner.m_hudCruiseStarted = {};
        return false;
    }
    return true;
}

bool StarfieldCruiseAdapter::Commands::RequestCourse(FormID courseId, OperationId operationId)
{
    return m_owner.DispatchCourse(courseId, operationId);
}
