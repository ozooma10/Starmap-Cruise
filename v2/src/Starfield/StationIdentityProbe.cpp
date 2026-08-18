#include "Starfield/StationIdentityProbe.h"

#include "Scaleform/ValueAccess.h"

#include "REX/REX.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef CFS_STATION_IDENTITY_PROBE
namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr const char* SetRouteDestinationEvent = "SetRouteDestination";
    constexpr std::uint32_t StationType = 4;
    constexpr FormID IsStarstationKeywordId = 0x003402A3;
    constexpr REL::ID StarMapMenuPrimaryVtable {446845};
    constexpr REL::ID GalaxyStatePrimaryVtable {446425};
    constexpr REL::ID SystemStatePrimaryVtable {447180};
    constexpr REL::ID BodyInspectStatePrimaryVtable {446055};
    constexpr REL::ID SurfaceMapStatePrimaryVtable {447074};
    constexpr std::size_t StarMapMenuActiveStateOffset = 0x1240;
    constexpr std::size_t StarMapMenuRouteDestinationOffset = 0x1250;
    constexpr std::size_t GalaxyStateSelectedSystemOffset = 0x880;
    constexpr std::size_t GalaxyStateQuickSelectOpenOffset = 0x8F8;
    constexpr std::size_t SystemStateDisplayedSystemOffset = 0xA10;
    constexpr std::size_t SystemStateDisplayRootOffset = 0xA18;
    constexpr std::size_t SystemStateSelectedBodyOffset = 0xA1C;
    constexpr std::size_t BodyInspectStateSelectedBodyOffset = 0xC90;
    constexpr auto RouteResultTimeout = std::chrono::seconds(5);
    constexpr std::size_t StableRoutePasses = 2;
    constexpr std::size_t MaxFields = 96;
    constexpr std::size_t MaxDepth = 2;
    constexpr std::int32_t MaxArrayElements = 12;
    constexpr std::size_t MaxStringLength = 96;
    enum class ScalarKind : std::uint8_t
    {
        Boolean,
        Number,
        String,
        Container,
        Empty,
    };

    struct ScalarField
    {
        std::string path;
        ScalarKind kind {ScalarKind::Empty};
        std::string value;
        std::optional<FormID> formIdCandidate;

        friend bool operator==(const ScalarField&, const ScalarField&) = default;
    };

    struct Snapshot
    {
        std::vector<ScalarField> fields;
        bool truncated {false};

        friend bool operator==(const Snapshot&, const Snapshot&) = default;
    };

    struct CaptureContext
    {
        Snapshot snapshot;
    };

    enum class NativeMapView : std::uint8_t
    {
        None,
        Galaxy,
        System,
        BodyInspect,
        Surface,
        Unknown,
    };

    struct NativeMapState
    {
        std::uintptr_t menuAddress {0};
        NativeMapView view {NativeMapView::None};
        FormID displayedSystem {0};
        FormID displayRoot {0};
        FormID selectedIdentity {0};
        FormID routeDestination {0};
        bool quickSelectOpen {false};

        friend bool operator==(const NativeMapState&, const NativeMapState&) = default;
    };

    struct ReferenceFacts
    {
        FormID id {0};
        FormID baseId {0};
        FormID cellId {0};
        bool liveReference {false};
        bool station {false};
        bool mapMarker {false};
    };

    struct TrackingSelection
    {
        MapSessionIdentity identity;
        FormID cellId {0};
        FormID baselineTarget {0};
        FormID baselineCourse {0};
        std::uint64_t baselineHudPublication {0};
        std::optional<NativeMapState> baselineNative;
    };

    struct RouteRequest
    {
        MapSessionIdentity identity;
        FormID cellId {0};
        FormID baselineTarget {0};
        FormID baselineCourse {0};
        std::uint64_t baselineHudPublication {0};
        std::optional<NativeMapState> baselineNative;
    };

    struct RouteAttempt
    {
        TrackingSelection selection;
        Clock::time_point started;
        std::optional<FormID> lastTarget;
        std::optional<FormID> lastCourse;
        std::optional<std::uint64_t> lastHudPublication;
        std::optional<NativeMapState> lastNative;
        std::size_t stablePasses {0};
        bool lastTargetMatches {false};
        bool lastCourseMatches {false};
        bool lastPostInputHudPublication {false};
        bool lastFresh {false};
    };

    struct PhysicalGesture
    {
        std::uint32_t device {0};
        std::int32_t idCode {0};
    };

    std::string CleanString(std::string_view value)
    {
        std::string clean;
        clean.reserve(std::min(value.size(), MaxStringLength) + 3);
        for (const auto ch : value.substr(0, MaxStringLength)) {
            const auto byte = static_cast<unsigned char>(ch);
            clean.push_back(byte < 0x20 ? ' ' : ch);
        }
        if (value.size() > MaxStringLength) {
            clean += "...";
        }
        return clean;
    }

    std::string CleanString(const wchar_t* value)
    {
        std::string clean;
        if (!value) {
            return clean;
        }
        for (std::size_t index = 0; value[index] != L'\0' && index < MaxStringLength; ++index) {
            const auto ch = value[index];
            clean.push_back(ch >= 0x20 && ch <= 0x7E ? static_cast<char>(ch) : '?');
        }
        return clean;
    }

    const char* KindName(ScalarKind kind)
    {
        switch (kind) {
        case ScalarKind::Boolean:
            return "bool";
        case ScalarKind::Number:
            return "number";
        case ScalarKind::String:
            return "string";
        case ScalarKind::Container:
            return "container";
        case ScalarKind::Empty:
            return "empty";
        }
        return "unknown";
    }

    void CaptureValue(CaptureContext& context, std::string path,
        const RE::Scaleform::GFx::Value& value, std::size_t depth);

    class MemberCollector final : public RE::Scaleform::GFx::Value::ObjectVisitor
    {
    public:
        MemberCollector(CaptureContext& context, std::string path, std::size_t depth) :
            m_context(context), m_path(std::move(path)), m_depth(depth)
        {}

        bool IncludeAS3PublicMembers() const override { return true; }

        void Visit(const char* name, const RE::Scaleform::GFx::Value& value) override
        {
            if (!name || m_context.snapshot.fields.size() >= MaxFields) {
                m_context.snapshot.truncated = true;
                return;
            }
            auto path = m_path.empty() ? std::string {name} : std::format("{}.{}", m_path, name);
            CaptureValue(m_context, std::move(path), value, m_depth);
        }

    private:
        CaptureContext& m_context;
        std::string m_path;
        std::size_t m_depth {0};
    };

    class ElementCollector final : public RE::Scaleform::GFx::Value::ArrayVisitor
    {
    public:
        ElementCollector(CaptureContext& context, std::string path, std::size_t depth) :
            m_context(context), m_path(std::move(path)), m_depth(depth)
        {}

        void Visit(std::uint32_t index, const RE::Scaleform::GFx::Value& value) override
        {
            if (m_context.snapshot.fields.size() >= MaxFields) {
                m_context.snapshot.truncated = true;
                return;
            }
            CaptureValue(m_context, std::format("{}[{}]", m_path, index), value, m_depth);
        }

    private:
        CaptureContext& m_context;
        std::string m_path;
        std::size_t m_depth {0};
    };

    void CaptureValue(CaptureContext& context, std::string path,
        const RE::Scaleform::GFx::Value& value, std::size_t depth)
    {
        if (context.snapshot.fields.size() >= MaxFields) {
            context.snapshot.truncated = true;
            return;
        }

        ScalarField field {.path = path};
        if (value.IsBoolean()) {
            field.kind = ScalarKind::Boolean;
            field.value = value.GetBoolean() ? "true" : "false";
        } else if (value.IsUInt() || value.IsInt() || value.IsNumber()) {
            field.kind = ScalarKind::Number;
            const auto number = CFS::ScaleformValue::AsNumber(value);
            field.value = std::format("{}", number);
            if (std::isfinite(number) && number > 0.0 &&
                number <= static_cast<double>(std::numeric_limits<FormID>::max()) &&
                std::floor(number) == number) {
                field.formIdCandidate = static_cast<FormID>(number);
            }
        } else if (value.IsString()) {
            field.kind = ScalarKind::String;
            const auto text = value.GetString();
            field.value = CleanString(text ? std::string_view {text} : std::string_view {});
        } else if (value.IsStringW()) {
            field.kind = ScalarKind::String;
            field.value = CleanString(value.GetStringW());
        } else if (value.IsArray() || value.IsDisplayObject() || value.GetType() == RE::Scaleform::GFx::Value::ValueType::kObject) {
            field.kind = ScalarKind::Container;
            field.value = value.IsArray() ? "array" : value.IsDisplayObject() ? "displayobject" : "object";
        } else {
            field.kind = ScalarKind::Empty;
            field.value = value.IsUndefined() ? "undefined" : "null-or-closure";
        }
        context.snapshot.fields.push_back(std::move(field));

        if (depth >= MaxDepth || context.snapshot.fields.size() >= MaxFields) {
            return;
        }
        auto copy = value;
        if (copy.IsArray()) {
            ElementCollector collector {context, std::move(path), depth + 1};
            copy.VisitElements(&collector, 0, MaxArrayElements);
        } else if (copy.IsObject()) {
            MemberCollector collector {context, std::move(path), depth + 1};
            copy.VisitMembers(&collector);
        }
    }

    Snapshot CaptureObject(RE::Scaleform::GFx::Value& value)
    {
        CaptureContext context;
        if (value.IsArray()) {
            ElementCollector collector {context, {}, 0};
            value.VisitElements(&collector, 0, MaxArrayElements);
        } else if (value.IsObject()) {
            MemberCollector collector {context, {}, 0};
            value.VisitMembers(&collector);
        }
        return std::move(context.snapshot);
    }

    class HighlightedStationCollector final : public RE::Scaleform::GFx::Value::ArrayVisitor
    {
    public:
        void Visit(std::uint32_t, const RE::Scaleform::GFx::Value& value) override
        {
            auto entry = value;
            bool highlighted = false;
            if (!CFS::ScaleformValue::BooleanMember(entry, "bIsInHighlightRadius", highlighted) || !highlighted) {
                return;
            }
            ++highlightedCount;
            if (CFS::ScaleformValue::UIntMember(entry, "uBodyType") == StationType) {
                station = CaptureObject(entry);
                stationCellId = CFS::ScaleformValue::UIntMember(entry, "uBodyID");
            }
        }

        std::size_t highlightedCount {0};
        std::optional<Snapshot> station;
        FormID stationCellId {0};
    };

    std::string DescribeForm(FormID id)
    {
        const auto form = RE::TESForm::LookupByID(id);
        if (!form) {
            return {};
        }

        const auto editorId = form->GetFormEditorID();
        const auto formType = static_cast<unsigned int>(std::to_underlying(form->GetFormType()));
        auto detail = std::format(" form-type={:02X} deleted={} edid='{}'",
            formType, form->IsDeleted(), editorId ? editorId : "");
        if (const auto reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(id)) {
            const auto base = reference->GetBaseObject();
            detail += std::format(" base={:08X} cell={:08X}",
                base ? base->GetFormID() : 0,
                reference->parentCell ? reference->parentCell->GetFormID() : 0);
        }
        return detail;
    }

    ReferenceFacts InspectReference(FormID id)
    {
        ReferenceFacts facts {.id = id};
        const auto reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
        if (!reference || reference->IsDeleted()) {
            return facts;
        }

        const auto base = reference->GetBaseObject();
        facts.liveReference = true;
        facts.baseId = base ? base->GetFormID() : 0;
        facts.cellId = reference->parentCell ? reference->parentCell->GetFormID() : 0;

        const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(IsStarstationKeywordId);
        facts.station = base && !base->IsDeleted() && keyword && reference->HasKeyword(keyword);
        if (const auto extra = reference->extraDataList.get()) {
            facts.mapMarker = extra->HasType(RE::ExtraDataType::kMapMarker);
        }
        return facts;
    }

    std::string DescribeReference(const ReferenceFacts& facts)
    {
        return std::format(
            "{:08X} live={} base={:08X} cell={:08X} station={} map-marker={}",
            facts.id, facts.liveReference, facts.baseId, facts.cellId,
            facts.station, facts.mapMarker);
    }

    std::optional<NativeMapState> ReadNativeMapState(
        const MapSessionIdentity& activeIdentity)
    {
        static_assert(StarMapMenuPrimaryVtable.id() == 446845);
        static_assert(GalaxyStatePrimaryVtable.id() == 446425);

        if (!activeIdentity.IsValid()) {
            return std::nullopt;
        }

        const auto ui = RE::UI::GetSingleton();
        const RE::BSFixedString mapName {MapMenuName};
        const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
        if (!ui || !ui->IsMenuOpen(mapName) || !menu || !menu->uiMovie ||
            !menu->uiMovie->asMovieRoot) {
            return std::nullopt;
        }

        const auto menuAddress = reinterpret_cast<std::uintptr_t>(menu.get());
        std::uintptr_t actualMenuVtable = 0;
        std::memcpy(&actualMenuVtable, reinterpret_cast<const void*>(menuAddress),
            sizeof(actualMenuVtable));
        static REL::Relocation<std::uintptr_t> expectedMenuVtable {
            StarMapMenuPrimaryVtable};
        if (actualMenuVtable != expectedMenuVtable.address()) {
            return std::nullopt;
        }

        NativeMapState state;
        state.menuAddress = menuAddress;
        std::memcpy(&state.routeDestination,
            reinterpret_cast<const void*>(menuAddress +
                StarMapMenuRouteDestinationOffset),
            sizeof(state.routeDestination));

        void* activeState = nullptr;
        std::memcpy(&activeState,
            reinterpret_cast<const void*>(menuAddress +
                StarMapMenuActiveStateOffset),
            sizeof(activeState));
        if (!activeState) {
            return state;
        }

        std::uintptr_t actualStateVtable = 0;
        std::memcpy(&actualStateVtable, activeState, sizeof(actualStateVtable));
        static REL::Relocation<std::uintptr_t> expectedGalaxyVtable {
            GalaxyStatePrimaryVtable};
        static REL::Relocation<std::uintptr_t> expectedSystemVtable {
            SystemStatePrimaryVtable};
        static REL::Relocation<std::uintptr_t> expectedBodyInspectVtable {
            BodyInspectStatePrimaryVtable};
        static REL::Relocation<std::uintptr_t> expectedSurfaceVtable {
            SurfaceMapStatePrimaryVtable};

        const auto stateAddress = reinterpret_cast<std::uintptr_t>(activeState);
        if (actualStateVtable == expectedGalaxyVtable.address()) {
            state.view = NativeMapView::Galaxy;
            std::memcpy(&state.selectedIdentity,
                reinterpret_cast<const void*>(stateAddress +
                    GalaxyStateSelectedSystemOffset),
                sizeof(state.selectedIdentity));
            std::uint8_t quickSelectOpen = 0;
            std::memcpy(&quickSelectOpen,
                reinterpret_cast<const void*>(stateAddress +
                    GalaxyStateQuickSelectOpenOffset),
                sizeof(quickSelectOpen));
            state.quickSelectOpen = quickSelectOpen != 0;
        } else if (actualStateVtable == expectedSystemVtable.address()) {
            state.view = NativeMapView::System;
            std::memcpy(&state.displayedSystem,
                reinterpret_cast<const void*>(stateAddress +
                    SystemStateDisplayedSystemOffset),
                sizeof(state.displayedSystem));
            std::memcpy(&state.displayRoot,
                reinterpret_cast<const void*>(stateAddress +
                    SystemStateDisplayRootOffset),
                sizeof(state.displayRoot));
            std::memcpy(&state.selectedIdentity,
                reinterpret_cast<const void*>(stateAddress +
                    SystemStateSelectedBodyOffset),
                sizeof(state.selectedIdentity));
        } else if (actualStateVtable == expectedBodyInspectVtable.address()) {
            state.view = NativeMapView::BodyInspect;
            std::memcpy(&state.selectedIdentity,
                reinterpret_cast<const void*>(stateAddress +
                    BodyInspectStateSelectedBodyOffset),
                sizeof(state.selectedIdentity));
        } else if (actualStateVtable == expectedSurfaceVtable.address()) {
            state.view = NativeMapView::Surface;
        } else {
            state.view = NativeMapView::Unknown;
        }
        return state;
    }

    const char* NativeMapViewName(NativeMapView view)
    {
        switch (view) {
        case NativeMapView::None:
            return "none";
        case NativeMapView::Galaxy:
            return "galaxy";
        case NativeMapView::System:
            return "system";
        case NativeMapView::BodyInspect:
            return "body-inspect";
        case NativeMapView::Surface:
            return "surface";
        case NativeMapView::Unknown:
            return "unknown";
        }
        return "unknown";
    }

    std::string DescribeNativeMapState(
        const std::optional<NativeMapState>& state)
    {
        if (!state) {
            return "unavailable";
        }

        return std::format(
            "view={} displayed-system={:08X}{} display-root={:08X}{} selected={:08X}{} quick-select={} route-destination={:08X}{}",
            NativeMapViewName(state->view), state->displayedSystem,
            DescribeForm(state->displayedSystem), state->displayRoot,
            DescribeForm(state->displayRoot), state->selectedIdentity,
            DescribeForm(state->selectedIdentity),
            state->quickSelectOpen, state->routeDestination,
            DescribeForm(state->routeDestination));
    }

    void LogSnapshot(std::string_view feed, const Snapshot& snapshot)
    {
        REX::INFO("[station-probe] {} fields={} truncated={}", feed,
            snapshot.fields.size(), snapshot.truncated);
        for (const auto& field : snapshot.fields) {
            const auto form = field.formIdCandidate ? DescribeForm(*field.formIdCandidate) : std::string {};
            REX::INFO("[station-probe] {}.{} {}='{}'{}", feed, field.path,
                KindName(field.kind), field.value, form);
        }
    }
}

struct StationIdentityProbe::State
{
    std::mutex mutex;
    MapSessionIdentity identity;
    std::optional<Snapshot> mapData;
    std::optional<Snapshot> marker;
    std::optional<Snapshot> dossier;
    FormID selectedStationCell {0};
    bool selectionPending {false};
    bool pending {false};
    bool cancelForIdentityChange {false};
    MapSessionIdentity readyIdentity;
    FormID readyStationCell {0};
    FormID readyTarget {0};
    FormID readyCourse {0};
    std::uint64_t readyHudPublication {0};
    std::optional<NativeMapState> readyNative;
    bool readyBaselineValid {false};
    FormID hudCourse {0};
    std::uint32_t hudGeneration {0};
    std::uint64_t hudPublication {0};
    bool hudCourseCurrent {false};
    std::optional<RouteRequest> routeRequest;
    std::optional<PhysicalGesture> routeGesture;
    std::optional<TrackingSelection> tracking;
    std::optional<RouteAttempt> attempt;
};
#else
struct StationIdentityProbe::State
{};
#endif

StationIdentityProbe::StationIdentityProbe()
#ifdef CFS_STATION_IDENTITY_PROBE
    : m_state(std::make_unique<State>())
#endif
{}

StationIdentityProbe::~StationIdentityProbe() = default;

bool StationIdentityProbe::Enabled() const
{
#ifdef CFS_STATION_IDENTITY_PROBE
    return true;
#else
    return false;
#endif
}

void StationIdentityProbe::CaptureMapData(const MapSessionIdentity& identity,
    RE::Scaleform::GFx::Value& data)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    auto snapshot = CaptureObject(data);
    std::lock_guard lock {m_state->mutex};
    if (m_state->identity != identity) {
        m_state->identity = identity;
        m_state->mapData.reset();
        m_state->marker.reset();
        m_state->dossier.reset();
        m_state->selectedStationCell = 0;
        m_state->selectionPending = true;
        m_state->pending = false;
        m_state->readyIdentity = {};
        m_state->readyStationCell = 0;
        m_state->readyBaselineValid = false;
        m_state->routeRequest.reset();
        m_state->cancelForIdentityChange = true;
    }
    m_state->mapData = std::move(snapshot);
#else
    (void)identity;
    (void)data;
#endif
}

void StationIdentityProbe::CaptureMarkers(const MapSessionIdentity& identity,
    RE::Scaleform::GFx::Value& markers)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    HighlightedStationCollector collector;
    markers.VisitElements(&collector);
    auto snapshot = collector.highlightedCount == 1 ? std::move(collector.station) : std::nullopt;
    const auto stationCellId = snapshot ? collector.stationCellId : FormID {0};

    std::lock_guard lock {m_state->mutex};
    if (m_state->identity != identity) {
        m_state->identity = identity;
        m_state->mapData.reset();
        m_state->marker.reset();
        m_state->dossier.reset();
        m_state->selectedStationCell = 0;
        m_state->selectionPending = true;
        m_state->pending = false;
        m_state->readyIdentity = {};
        m_state->readyStationCell = 0;
        m_state->readyBaselineValid = false;
        m_state->routeRequest.reset();
        m_state->cancelForIdentityChange = true;
    }
    if (m_state->marker == snapshot &&
        m_state->selectedStationCell == stationCellId) {
        return;
    }
    m_state->marker = std::move(snapshot);
    m_state->dossier.reset();
    m_state->selectedStationCell = stationCellId;
    m_state->selectionPending = true;
    m_state->pending = m_state->marker.has_value();
    m_state->readyIdentity = {};
    m_state->readyStationCell = 0;
    m_state->readyBaselineValid = false;
#else
    (void)identity;
    (void)markers;
#endif
}

void StationIdentityProbe::CaptureDossier(const MapSessionIdentity& identity,
    RE::Scaleform::GFx::Value& data)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    std::optional<Snapshot> snapshot;
    if (CFS::ScaleformValue::UIntMember(data, "iType") == StationType) {
        snapshot = CaptureObject(data);
    }

    std::lock_guard lock {m_state->mutex};
    if (m_state->identity != identity) {
        m_state->identity = identity;
        m_state->mapData.reset();
        m_state->marker.reset();
        m_state->dossier.reset();
        m_state->selectedStationCell = 0;
        m_state->selectionPending = true;
        m_state->pending = false;
        m_state->readyIdentity = {};
        m_state->readyStationCell = 0;
        m_state->readyBaselineValid = false;
        m_state->routeRequest.reset();
        m_state->cancelForIdentityChange = true;
    }
    if (m_state->dossier == snapshot) {
        return;
    }
    m_state->dossier = std::move(snapshot);
    m_state->pending = m_state->marker.has_value();
#else
    (void)identity;
    (void)data;
#endif
}

void StationIdentityProbe::CaptureHudCourse(std::uint32_t generation,
    FormID courseId, std::uint64_t publication)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    std::lock_guard lock {m_state->mutex};
    if (generation == 0 || generation != m_state->hudGeneration ||
        publication == 0 ||
        publication < m_state->hudPublication) {
        return;
    }
    m_state->hudGeneration = generation;
    m_state->hudCourse = courseId;
    m_state->hudPublication = publication;
    m_state->hudCourseCurrent = true;
#else
    (void)generation;
    (void)courseId;
    (void)publication;
#endif
}

void StationIdentityProbe::OnHudMovieCreated(std::uint32_t generation)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    bool cancelled = false;
    {
        std::lock_guard lock {m_state->mutex};
        cancelled = m_state->routeRequest.has_value() ||
            m_state->attempt.has_value();
        m_state->hudGeneration = generation;
        m_state->hudCourse = 0;
        m_state->hudCourseCurrent = false;
        m_state->readyBaselineValid = false;
        m_state->routeRequest.reset();
        m_state->attempt.reset();
    }
    if (cancelled) {
        REX::INFO("[station-route-probe] cancelled because the HUD movie was replaced");
    }
#else
    (void)generation;
#endif
}

bool StationIdentityProbe::ObserveStockRouteInput(
    const RE::ButtonEvent& event, std::uint64_t latestHudPublication)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    const auto device = static_cast<std::uint32_t>(event.deviceType);
    const auto idCode = event.idCode;
    const bool down = event.value != 0.0f;
    const bool first = down && event.heldDownSecs == 0.0f;

    std::lock_guard lock {m_state->mutex};
    if (m_state->routeGesture &&
        m_state->routeGesture->device == device &&
        m_state->routeGesture->idCode == idCode) {
        if (!down) {
            m_state->routeGesture.reset();
        }
        return true;
    }

    const auto name = event.strUserEvent.c_str();
    if (!first || event.disabled || !name ||
        std::strcmp(name, SetRouteDestinationEvent) != 0 ||
        !m_state->readyIdentity.IsValid() || m_state->readyStationCell == 0 ||
        !m_state->readyBaselineValid) {
        return false;
    }

    m_state->routeRequest = RouteRequest {
        .identity = m_state->readyIdentity,
        .cellId = m_state->readyStationCell,
        .baselineTarget = m_state->readyTarget,
        .baselineCourse = m_state->readyCourse,
        .baselineHudPublication = latestHudPublication,
        .baselineNative = m_state->readyNative,
    };
    m_state->routeGesture = PhysicalGesture {
        .device = device,
        .idCode = idCode,
    };
    return true;
#else
    (void)event;
    (void)latestHudPublication;
    return false;
#endif
}

void StationIdentityProbe::OnMapClosed(const MapSessionIdentity& identity)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    std::lock_guard lock {m_state->mutex};
    if (m_state->identity != identity) {
        return;
    }

    m_state->identity = {};
    m_state->mapData.reset();
    m_state->marker.reset();
    m_state->dossier.reset();
    m_state->selectedStationCell = 0;
    m_state->selectionPending = false;
    m_state->pending = false;
    m_state->readyIdentity = {};
    m_state->readyStationCell = 0;
    m_state->readyBaselineValid = false;
#else
    (void)identity;
#endif
}

void StationIdentityProbe::CancelRouteAttempt()
{
#ifdef CFS_STATION_IDENTITY_PROBE
    bool cancelled = false;
    {
        std::lock_guard lock {m_state->mutex};
        cancelled = m_state->routeRequest.has_value() ||
            m_state->attempt.has_value();
        m_state->routeRequest.reset();
        m_state->routeGesture.reset();
        m_state->readyIdentity = {};
        m_state->readyStationCell = 0;
        m_state->readyBaselineValid = false;
        m_state->tracking.reset();
        m_state->attempt.reset();
    }
    if (cancelled) {
        REX::INFO("[station-route-probe] cancelled because the plugin Cruise action was used");
    }
#endif
}

void StationIdentityProbe::Drain(const MapSessionIdentity& activeIdentity)
{
#ifdef CFS_STATION_IDENTITY_PROBE
    MapSessionIdentity identity;
    std::optional<Snapshot> mapData;
    std::optional<Snapshot> marker;
    std::optional<Snapshot> dossier;
    FormID selectedStationCell = 0;
    bool selectionPending = false;
    bool cancelForIdentityChange = false;
    std::optional<RouteRequest> routeRequest;
    FormID hudCourse = 0;
    std::uint32_t hudGeneration = 0;
    std::uint64_t hudPublication = 0;
    bool hudCourseCurrent = false;
    {
        std::lock_guard lock {m_state->mutex};
        if (activeIdentity.IsValid() && m_state->identity == activeIdentity) {
            identity = m_state->identity;
            selectedStationCell = m_state->selectedStationCell;
            selectionPending = m_state->selectionPending;
            m_state->selectionPending = false;
            if (m_state->pending && m_state->marker) {
                mapData = m_state->mapData;
                marker = m_state->marker;
                dossier = m_state->dossier;
                m_state->pending = false;
            }
        }
        routeRequest = std::exchange(m_state->routeRequest, std::nullopt);
        cancelForIdentityChange = std::exchange(
            m_state->cancelForIdentityChange, false);
        hudCourse = m_state->hudCourse;
        hudGeneration = m_state->hudGeneration;
        hudPublication = m_state->hudPublication;
        hudCourseCurrent = m_state->hudCourseCurrent;
    }

    if (cancelForIdentityChange) {
        const bool cancelled = m_state->attempt.has_value();
        m_state->tracking.reset();
        m_state->attempt.reset();
        routeRequest.reset();
        if (cancelled) {
            REX::INFO("[station-route-probe] cancelled because a new map session replaced the observed session");
        }
    }

    if (marker) {
        REX::INFO("[station-probe] capture session={} generation={} map={} dossier={}",
            identity.session, identity.generation, mapData.has_value(), dossier.has_value());
        if (mapData) {
            LogSnapshot("map", *mapData);
        }
        LogSnapshot("marker", *marker);
        if (dossier) {
            LogSnapshot("dossier", *dossier);
        }
    }

    if (selectionPending && !m_state->attempt) {
        if (selectedStationCell != 0 && identity == activeIdentity) {
            const auto target = RE::ShipHudTarget::GetCurrent();
            const auto native = ReadNativeMapState(activeIdentity);
            m_state->tracking = TrackingSelection {
                .identity = identity,
                .cellId = selectedStationCell,
                .baselineTarget = target,
                .baselineCourse = hudCourse,
                .baselineHudPublication = hudPublication,
                .baselineNative = native,
            };
            {
                std::lock_guard lock {m_state->mutex};
                m_state->readyIdentity = identity;
                m_state->readyStationCell = selectedStationCell;
                m_state->readyTarget = target;
                m_state->readyCourse = hudCourse;
                m_state->readyHudPublication = hudPublication;
                m_state->readyNative = native;
                m_state->readyBaselineValid = hudCourseCurrent;
            }

            const auto targetFacts = InspectReference(target);
            const auto courseFacts = InspectReference(hudCourse);
            if (hudCourseCurrent) {
                REX::INFO("[station-route-probe] READY session={} generation={} cell={:08X} baseline-target={} baseline-course={} hud-generation={} native={}; press the vanilla SET COURSE binding",
                    identity.session, identity.generation, selectedStationCell,
                    DescribeReference(targetFacts), DescribeReference(courseFacts),
                    hudGeneration, DescribeNativeMapState(native));
            } else {
                REX::INFO("[station-route-probe] WAITING session={} generation={} cell={:08X}; no validated course publication exists for HUD generation {}",
                    identity.session, identity.generation, selectedStationCell,
                    hudGeneration);
            }
        } else {
            m_state->tracking.reset();
            std::lock_guard lock {m_state->mutex};
            m_state->readyIdentity = {};
            m_state->readyStationCell = 0;
            m_state->readyBaselineValid = false;
        }
    }

    if (!selectionPending && !routeRequest && !m_state->attempt &&
        m_state->tracking && m_state->tracking->identity == activeIdentity) {
        const auto target = RE::ShipHudTarget::GetCurrent();
        const auto native = ReadNativeMapState(activeIdentity);
        m_state->tracking->baselineTarget = target;
        m_state->tracking->baselineCourse = hudCourse;
        m_state->tracking->baselineHudPublication = hudPublication;
        m_state->tracking->baselineNative = native;

        bool becameReady = false;
        {
            std::lock_guard lock {m_state->mutex};
            if (m_state->readyIdentity == m_state->tracking->identity &&
                m_state->readyStationCell == m_state->tracking->cellId &&
                hudCourseCurrent) {
                becameReady = !m_state->readyBaselineValid;
                m_state->readyTarget = target;
                m_state->readyCourse = hudCourse;
                m_state->readyHudPublication = hudPublication;
                m_state->readyNative = native;
                m_state->readyBaselineValid = true;
            }
        }
        if (becameReady) {
            const auto targetFacts = InspectReference(target);
            const auto courseFacts = InspectReference(hudCourse);
            REX::INFO("[station-route-probe] READY session={} generation={} cell={:08X} baseline-target={} baseline-course={} hud-generation={} native={}; press the vanilla SET COURSE binding",
                m_state->tracking->identity.session,
                m_state->tracking->identity.generation,
                m_state->tracking->cellId, DescribeReference(targetFacts),
                DescribeReference(courseFacts), hudGeneration,
                DescribeNativeMapState(native));
        }
    }

    if (routeRequest) {
        if (!m_state->tracking ||
            m_state->tracking->identity != routeRequest->identity ||
            m_state->tracking->cellId != routeRequest->cellId) {
            REX::WARN("[station-route-probe] ignored vanilla SET COURSE for cell={:08X}; wait for the READY line before pressing it",
                routeRequest->cellId);
        } else {
            m_state->attempt = RouteAttempt {
                .selection = TrackingSelection {
                    .identity = routeRequest->identity,
                    .cellId = routeRequest->cellId,
                    .baselineTarget = routeRequest->baselineTarget,
                    .baselineCourse = routeRequest->baselineCourse,
                    .baselineHudPublication = routeRequest->baselineHudPublication,
                    .baselineNative = routeRequest->baselineNative,
                },
                .started = Clock::now(),
            };
            {
                std::lock_guard lock {m_state->mutex};
                m_state->readyIdentity = {};
                m_state->readyStationCell = 0;
                m_state->readyBaselineValid = false;
            }
            REX::INFO("[station-route-probe] BEGIN session={} generation={} cell={:08X}; vanilla input passed through unchanged",
                routeRequest->identity.session, routeRequest->identity.generation,
                routeRequest->cellId);
        }
    }

    if (!m_state->attempt) {
        return;
    }

    auto& attempt = *m_state->attempt;
    const auto target = RE::ShipHudTarget::GetCurrent();
    const auto native = activeIdentity == attempt.selection.identity ?
        ReadNativeMapState(activeIdentity) : std::nullopt;
    const bool changed = !attempt.lastTarget || !attempt.lastCourse ||
        !attempt.lastHudPublication ||
        *attempt.lastTarget != target || *attempt.lastCourse != hudCourse ||
        *attempt.lastHudPublication != hudPublication ||
        attempt.lastNative != native;

    if (changed) {
        const auto targetFacts = InspectReference(target);
        const auto courseFacts = InspectReference(hudCourse);
        attempt.lastTargetMatches = targetFacts.liveReference &&
            targetFacts.station &&
            targetFacts.cellId == attempt.selection.cellId;
        attempt.lastCourseMatches = courseFacts.liveReference &&
            courseFacts.mapMarker &&
            target != hudCourse &&
            courseFacts.cellId == attempt.selection.cellId;
        attempt.lastPostInputHudPublication =
            hudPublication > attempt.selection.baselineHudPublication;
        attempt.lastFresh = attempt.lastPostInputHudPublication &&
            (target != attempt.selection.baselineTarget ||
                hudCourse != attempt.selection.baselineCourse);
        attempt.lastTarget = target;
        attempt.lastCourse = hudCourse;
        attempt.lastHudPublication = hudPublication;
        attempt.lastNative = native;

        REX::INFO("[station-route-probe] OBSERVE cell={:08X} target={} target-match={} course={} course-match={} hud-publication={} post-input={} fresh={} native={}",
            attempt.selection.cellId, DescribeReference(targetFacts),
            attempt.lastTargetMatches, DescribeReference(courseFacts),
            attempt.lastCourseMatches, hudPublication,
            attempt.lastPostInputHudPublication, attempt.lastFresh,
            DescribeNativeMapState(native));
    }

    if (attempt.lastTargetMatches && attempt.lastCourseMatches &&
        attempt.lastFresh) {
        ++attempt.stablePasses;
    } else {
        attempt.stablePasses = 0;
    }

    if (attempt.stablePasses >= StableRoutePasses) {
        REX::INFO("[station-route-probe] RESOLVED cell={:08X} station-ref={:08X} course-xmrk={:08X} native={}",
            attempt.selection.cellId, *attempt.lastTarget, *attempt.lastCourse,
            DescribeNativeMapState(attempt.lastNative));
        m_state->attempt.reset();
        return;
    }

    if (Clock::now() - attempt.started >= RouteResultTimeout) {
        REX::WARN("[station-route-probe] INCOMPLETE cell={:08X} target={:08X} target-match={} course={:08X} course-match={} hud-publication={} post-input={} fresh={} native={}",
            attempt.selection.cellId, attempt.lastTarget.value_or(0),
            attempt.lastTargetMatches, attempt.lastCourse.value_or(0),
            attempt.lastCourseMatches,
            attempt.lastHudPublication.value_or(0),
            attempt.lastPostInputHudPublication, attempt.lastFresh,
            DescribeNativeMapState(attempt.lastNative));
        m_state->attempt.reset();
    }
#else
    (void)activeIdentity;
#endif
}

void StationIdentityProbe::Invalidate()
{
#ifdef CFS_STATION_IDENTITY_PROBE
    std::lock_guard lock {m_state->mutex};
    m_state->identity = {};
    m_state->mapData.reset();
    m_state->marker.reset();
    m_state->dossier.reset();
    m_state->selectedStationCell = 0;
    m_state->selectionPending = false;
    m_state->pending = false;
    m_state->readyIdentity = {};
    m_state->readyStationCell = 0;
    m_state->readyBaselineValid = false;
    m_state->routeRequest.reset();
    m_state->routeGesture.reset();
    m_state->cancelForIdentityChange = false;
    m_state->tracking.reset();
    m_state->attempt.reset();
#endif
}
