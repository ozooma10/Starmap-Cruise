// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns selection, runtime guards, and destination state.

        const char* DestinationKindName(BodyKind a_kind)
        {
            switch (a_kind) {
            case BodyKind::kPlanet:
                return "planet";
            case BodyKind::kMoon:
                return "moon";
            case BodyKind::kStation:
                return "station";
            default:
                return "non-planet target";
            }
        }

        bool IsPlanetary(const BodyDestination& a_destination)
        {
            return a_destination.kind == BodyKind::kPlanet ||
                   a_destination.kind == BodyKind::kMoon;
        }

        std::uint32_t CourseTargetID(const BodyDestination& a_destination)
        {
            return a_destination.courseFormID ? a_destination.courseFormID :
                                                a_destination.formID;
        }

        bool UsesRemoteSystemRoute(const BodyDestination& a_destination)
        {
            return IsPlanetary(a_destination) ||
                   a_destination.kind == BodyKind::kStation;
        }

        std::optional<std::uint32_t> MapTreeSystemID(std::uint32_t a_formID)
        {
            return BodyIndex::LookupSystemRoot(a_formID);
        }

        struct ControlMapArray
        {
            std::uint32_t size;
            std::uint32_t capacity;
            std::uintptr_t data;
        };
        static_assert(sizeof(ControlMapArray) == 0x10);

        struct ControlMapMapping
        {
            std::uintptr_t eventEntry;
            std::uint32_t keyCode;
            std::uint32_t modifierCode;
            std::uint8_t bindingSlot;
            std::uint8_t unk11;
            std::uint16_t unk12;
            std::uint8_t sortIndex;
            std::uint8_t unk15[3];
            std::uint32_t contextMask;
            std::uint8_t bindingMeta;
            std::uint8_t visibleInControls;
            std::uint8_t defaultWasUnbound;
            std::uint8_t unk1F;
            std::uint8_t required;
            std::uint8_t pad21[7];
        };
        static_assert(sizeof(ControlMapMapping) == kControlMapMappingStride);

        void ResolveCurrentSystem(const std::vector<HudRow>& a_rows)
        {
            if (!BodyIndex::Ready())
                return;
            std::unordered_map<std::uint32_t, std::size_t> systems;
            for (const auto& row : a_rows)
                if (const auto body = BodyIndex::Lookup(row.id))
                    ++systems[body->galaxy.system];
            if (systems.empty())
                return;

            auto best = systems.begin();
            bool unique = true;
            for (auto it = std::next(systems.begin()); it != systems.end(); ++it) {
                if (it->second > best->second) {
                    best = it;
                    unique = true;
                } else if (it->second == best->second) {
                    unique = false;
                }
            }
            if (!unique) {
                g_haveCurrentSystem.store(false, std::memory_order_release);
                return;
            }
            const auto old = g_currentSystem.exchange(best->first, std::memory_order_acq_rel);
            const bool had = g_haveCurrentSystem.exchange(true, std::memory_order_acq_rel);
            if ((!had || old != best->first) && Settings::Verbose())
                REX::INFO("[system] cockpit feed resolves current system {}", best->first);

            // A fast Starmap open can race the load-order index: the HUD rows
            // already exist, but ResolveCurrentSystem cannot join them until
            // BodyIndex becomes ready. Recover only an unresolved snapshot from
            // the same still-open movie/session. Never rewrite a captured system.
            bool recoveredMapSession = false;
            if (g_mapOpen.load(std::memory_order_acquire)) {
                std::lock_guard lock{ g_mapMutex };
                if (g_mapOpen.load(std::memory_order_acquire) &&
                    !g_map.haveCapturedSystem &&
                    g_map.session != 0 &&
                    g_map.session == g_mapSession.load(std::memory_order_acquire) &&
                    g_map.generation ==
                        g_mapMovie.generation.load(std::memory_order_acquire)) {
                    g_map.haveCapturedSystem = true;
                    g_map.capturedSystem = best->first;
                    recoveredMapSession = true;
                }
            }
            if (recoveredMapSession) {
                g_mapActionHintSignature.store(0, std::memory_order_release);
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::INFO("[map] recovered current system {} for the open Starmap session",
                    best->first);
            }
        }

        double AsNumber(const V& a_value)
        {
            if (a_value.IsUInt())
                return a_value.GetUInt();
            if (a_value.IsInt())
                return a_value.GetInt();
            if (a_value.IsNumber())
                return a_value.GetNumber();
            return 0.0;
        }

        std::uint32_t UIntMember(V& a_object, const char* a_name)
        {
            V member;
            return a_object.GetMember(a_name, &member) ? static_cast<std::uint32_t>(AsNumber(member)) : 0;
        }

        std::string StringMember(V& a_object, const char* a_name)
        {
            V member;
            if (!a_object.GetMember(a_name, &member) || !member.IsString())
                return {};
            const char* text = member.GetString();
            return text ? text : "";
        }

        bool ObjectMember(V& a_object, const char* a_name, V& a_member)
        {
            return a_object.GetMember(a_name, &a_member) &&
                   (a_member.IsObject() || a_member.IsDisplayObject());
        }

        bool BooleanMember(V& a_object, const char* a_name, bool& a_value)
        {
            V member;
            if (!a_object.GetMember(a_name, &member) || !member.IsBoolean())
                return false;
            a_value = member.GetBoolean();
            return true;
        }

        bool IsReadableRange(std::uintptr_t a_address, std::size_t a_size)
        {
            if (!a_address || !a_size || a_address > UINTPTR_MAX - a_size)
                return false;

            const auto end = a_address + a_size;
            while (a_address < end) {
                MEMORY_BASIC_INFORMATION memory{};
                if (::VirtualQuery(reinterpret_cast<const void*>(a_address),
                        &memory, sizeof(memory)) != sizeof(memory) ||
                    memory.State != MEM_COMMIT ||
                    (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
                    return false;

                const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) +
                                       memory.RegionSize;
                if (regionEnd <= a_address)
                    return false;
                a_address = std::min(end, regionEnd);
            }
            return true;
        }

        template <class T>
        bool ReadMemory(std::uintptr_t a_address, T& a_value)
        {
            if (!IsReadableRange(a_address, sizeof(T)))
                return false;
            std::memcpy(&a_value, reinterpret_cast<const void*>(a_address), sizeof(T));
            return true;
        }

        template <std::size_t N>
        std::string HexBytes(const std::array<std::uint8_t, N>& a_bytes)
        {
            std::string result;
            for (const auto byte : a_bytes)
                result += std::format("{}{:02X}", result.empty() ? "" : " ", byte);
            return result;
        }

        std::string ReadControlMapEvent(std::uintptr_t a_entry)
        {
            for (std::size_t depth = 0; a_entry && depth < 8; ++depth) {
                std::uint8_t flags = 0;
                if (!ReadMemory(a_entry + 0x14, flags))
                    return {};
                if ((flags & 0x02) == 0) {
                    std::uint32_t length = 0;
                    if (!ReadMemory(a_entry + 0x08, length) || length > 128 ||
                        !IsReadableRange(a_entry + 0x18, length))
                        return {};
                    return std::string{ reinterpret_cast<const char*>(a_entry + 0x18), length };
                }
                if (!ReadMemory(a_entry + 0x08, a_entry))
                    return {};
            }
            return {};
        }

        bool FindCruiseBinding(std::uintptr_t a_controlMap, std::uint8_t a_context,
            std::uint32_t a_deviceIndex, const char* a_userEvent,
            std::int32_t& a_key, std::int32_t& a_modifier)
        {
            if (a_deviceIndex > 2)
                return false;

            std::uintptr_t context = 0;
            if (!ReadMemory(a_controlMap + kControlMapContextSlotsOffset +
                    static_cast<std::size_t>(a_context) * sizeof(std::uintptr_t), context) ||
                !context)
                return false;

            // A context begins with keyboard, mouse, and gamepad array headers.
            ControlMapArray mappings{};
            if (!ReadMemory(context +
                    static_cast<std::size_t>(a_deviceIndex) * sizeof(ControlMapArray),
                    mappings) ||
                mappings.size > mappings.capacity ||
                mappings.size > kMaxControlMappings ||
                (mappings.size && !IsReadableRange(mappings.data,
                    static_cast<std::size_t>(mappings.size) * kControlMapMappingStride)))
                return false;

            for (const auto desiredSlot : { std::uint8_t{ 0 }, std::uint8_t{ 1 } }) {
                for (std::uint32_t i = 0; i < mappings.size; ++i) {
                    ControlMapMapping mapping{};
                    std::memcpy(&mapping,
                        reinterpret_cast<const void*>(mappings.data +
                            static_cast<std::size_t>(i) * kControlMapMappingStride),
                        sizeof(mapping));
                    if (mapping.bindingSlot != desiredSlot ||
                        ReadControlMapEvent(mapping.eventEntry) != a_userEvent ||
                        mapping.keyCode == 0xFF || mapping.keyCode == 0x7FFFFFFF ||
                        (a_deviceIndex == 0 && mapping.keyCode > 0xFE))
                        continue;

                    a_key = static_cast<std::int32_t>(mapping.keyCode);
                    a_modifier = mapping.modifierCode == 0xFF ||
                                         mapping.modifierCode == 0x7FFFFFFF ?
                                     -1 :
                                     static_cast<std::int32_t>(mapping.modifierCode);
                    return a_deviceIndex != 0 || a_modifier <= 0xFE;
                }
            }
            return false;
        }

        void ResolveCruiseMapBinding()
        {
            REL::Relocation<std::uintptr_t*> singleton{ kControlMapSingletonPtr };
            std::uintptr_t controlMap = 0;
            std::uintptr_t vtable = 0;
            if (!ReadMemory(singleton.address(), controlMap) ||
                !IsReadableRange(controlMap, kControlMapSize) ||
                !ReadMemory(controlMap, vtable) ||
                vtable != RE::VTABLE::ControlMap[0].address()) {
                g_cruiseMapKey.store(-1, std::memory_order_release);
                g_cruiseMapModifier.store(-1, std::memory_order_release);
                g_cruiseMapMouseButton.store(-1, std::memory_order_release);
                g_cruiseMapGamepadButton.store(-1, std::memory_order_release);
                REX::WARN("[input] live Cruise bindings unavailable: ControlMap validation failed");
                return;
            }

            std::int32_t key = -1;
            std::int32_t modifier = -1;
            std::int32_t mouseButton = -1;
            std::int32_t mouseModifier = -1;
            std::int32_t gamepadButton = -1;
            std::int32_t gamepadModifier = -1;
            for (const auto context : kCruiseControlContexts)
                if (FindCruiseBinding(controlMap, context, 0, kCruiseMapUserEvent,
                        key, modifier))
                    break;
            for (const auto context : kCruiseControlContexts)
                if (FindCruiseBinding(controlMap, context, 1, kCruiseMapUserEvent,
                        mouseButton, mouseModifier))
                    break;
            for (const auto context : kCruiseControlContexts)
                if (FindCruiseBinding(controlMap, context, 2,
                        kCruiseMapGamepadUserEvent, gamepadButton, gamepadModifier))
                    break;

            // The UI hook can identify one physical ButtonEvent at a time. Do
            // not claim a mouse/gamepad chord unless its second edge can also
            // be proven; the shipped SHMonocle binding is a single button.
            if (mouseButton >= 0 && mouseModifier >= 0) {
                REX::WARN("[input] mouse Cruise chord is unsupported; mouse routing disabled");
                mouseButton = -1;
            }
            if (gamepadButton >= 0 && gamepadModifier >= 0) {
                REX::WARN("[input] controller '{}' chord is unsupported; controller routing disabled",
                    kCruiseMapGamepadUserEvent);
                gamepadButton = -1;
            }

            const auto oldKey = g_cruiseMapKey.exchange(key, std::memory_order_acq_rel);
            const auto oldModifier = g_cruiseMapModifier.exchange(modifier, std::memory_order_acq_rel);
            const auto oldMouse = g_cruiseMapMouseButton.exchange(mouseButton,
                std::memory_order_acq_rel);
            const auto oldGamepad = g_cruiseMapGamepadButton.exchange(gamepadButton,
                std::memory_order_acq_rel);
            if (key >= 0 && (oldKey != key || oldModifier != modifier)) {
                REX::INFO("[input] Starmap Cruise action follows live Cruise binding: VK=0x{:02X} modifier={}",
                    key, modifier < 0 ? "none" : std::format("0x{:02X}", modifier));
            }
            if (mouseButton >= 0 && oldMouse != mouseButton)
                REX::INFO("[input] Starmap Cruise action follows live mouse Cruise binding: id={}",
                    mouseButton);
            if (gamepadButton >= 0 && oldGamepad != gamepadButton)
                REX::INFO("[input] Starmap Cruise action follows live controller '{}' binding: id={} modifier={}",
                    kCruiseMapGamepadUserEvent, gamepadButton,
                    gamepadModifier < 0 ? "none" : std::format("{}", gamepadModifier));
            if (key < 0 && mouseButton < 0 && gamepadButton < 0)
                REX::WARN("[input] Cruise has no keyboard, mouse, or controller binding; Starmap Cruise action disabled");
        }

        bool Payload(const RE::Scaleform::GFx::FunctionHandler::Params& a_params, V& a_data)
        {
            if (!a_params.args || a_params.argCount == 0)
                return false;
            a_data = a_params.args[0];
            V inner;
            if (a_data.IsObject() && a_data.GetMember("data", &inner))
                a_data = inner;
            return a_data.IsObject() || a_data.IsArray();
        }

        bool ValidateIsInSpaceBinding()
        {
            static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);

            REL::Relocation<std::uintptr_t> target{ RE::ID::TESObjectREFR::IsInSpace };
            const auto address = target.address();
            const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
            if (!address || !module) {
                REX::ERROR("[space] IsInSpace binding unavailable; bridge disabled before hooks");
                return false;
            }

            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
                REX::ERROR("[space] Starfield module has no valid DOS header; bridge disabled");
                return false;
            }
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                REX::ERROR("[space] Starfield module has no valid NT header; bridge disabled");
                return false;
            }
            const auto imageEnd = module + nt->OptionalHeader.SizeOfImage;
            if (address < module || address > imageEnd ||
                kIsInSpace116244Prologue.size() > imageEnd - address) {
                REX::ERROR("[space] Address Library ID 63482 resolved outside Starfield.exe: {:016X}; bridge disabled",
                    address);
                return false;
            }
            if (std::memcmp(reinterpret_cast<const void*>(address),
                    kIsInSpace116244Prologue.data(), kIsInSpace116244Prologue.size()) != 0) {
                REX::ERROR("[space] Address Library ID 63482 failed the Starfield 1.16.244 prologue fingerprint at {:016X}; bridge disabled",
                    address);
                return false;
            }

            g_isInSpace.store(reinterpret_cast<IsInSpace_t>(address), std::memory_order_release);
            REX::INFO("[space] IsInSpace(false) binding validated: Address Library ID 63482, RVA=0x{:X}, fingerprint={} bytes",
                address - module, kIsInSpace116244Prologue.size());
            return true;
        }

        bool ValidateShipTargetBinding()
        {
            REL::Relocation<std::uintptr_t> target{ kSetShipHudTarget };
            const auto address = target.address();
            const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
            if (!address || !module) {
                REX::ERROR("[target] native ship-target binding unavailable; bridge disabled before hooks");
                return false;
            }

            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
                REX::ERROR("[target] Starfield module has no valid DOS header; bridge disabled");
                return false;
            }
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                REX::ERROR("[target] Starfield module has no valid NT header; bridge disabled");
                return false;
            }
            const auto imageEnd = module + nt->OptionalHeader.SizeOfImage;
            constexpr std::size_t kFingerprintSpan = 12;
            if (address < module || address >= imageEnd ||
                kFingerprintSpan > imageEnd - address) {
                REX::ERROR("[target] Address Library ID 97892 resolved outside Starfield.exe: {:016X}; bridge disabled",
                    address);
                return false;
            }
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
            if (std::memcmp(bytes, kSetShipHudTarget116244Prefix.data(),
                    kSetShipHudTarget116244Prefix.size()) != 0 ||
                bytes[10] != 0x85 || bytes[11] != 0xC9) {
                REX::ERROR("[target] Address Library ID 97892 failed the Starfield 1.16.244 fingerprint at {:016X}; bridge disabled",
                    address);
                return false;
            }

            g_setShipHudTarget.store(reinterpret_cast<SetShipHudTarget_t>(address),
                std::memory_order_release);
            REX::INFO("[target] native ship-target setter validated: Address Library ID 97892, RVA=0x{:X}",
                address - module);
            return true;
        }

        bool ValidateGalaxySystemSelectionBindings()
        {
            static_assert(kSelectGalaxySystem.id() == 94292);
            static_assert(kCloseGalaxyQuickSelect.id() == 94308);
            static_assert(kStarMapMenuPrimaryVtable.id() == 446845);
            static_assert(kGalaxyStatePrimaryVtable.id() == 446425);

            REL::Relocation<std::uintptr_t> selectTarget{ kSelectGalaxySystem };
            REL::Relocation<std::uintptr_t> closeTarget{ kCloseGalaxyQuickSelect };
            REL::Relocation<std::uintptr_t> menuVtable{ kStarMapMenuPrimaryVtable };
            REL::Relocation<std::uintptr_t> galaxyVtable{ kGalaxyStatePrimaryVtable };
            const auto selectAddress = selectTarget.address();
            const auto closeAddress = closeTarget.address();
            const auto menuVtableAddress = menuVtable.address();
            const auto galaxyVtableAddress = galaxyVtable.address();
            const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
            if (!selectAddress || !closeAddress || !menuVtableAddress ||
                !galaxyVtableAddress || !module) {
                REX::ERROR("[jump] native galaxy-system selection bindings unavailable; bridge disabled before hooks");
                return false;
            }

            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
                REX::ERROR("[jump] Starfield module has no valid DOS header; bridge disabled");
                return false;
            }
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                REX::ERROR("[jump] Starfield module has no valid NT header; bridge disabled");
                return false;
            }
            const auto imageEnd = module + nt->OptionalHeader.SizeOfImage;
            const auto inImage = [&](std::uintptr_t a_value, std::size_t a_span) {
                return a_value >= module && a_value < imageEnd &&
                       a_span <= imageEnd - a_value;
            };
            if (!inImage(selectAddress, kSelectGalaxySystem116244Prologue.size()) ||
                !inImage(closeAddress, kCloseGalaxyQuickSelect116244Prologue.size()) ||
                !inImage(menuVtableAddress, sizeof(std::uintptr_t)) ||
                !inImage(galaxyVtableAddress, sizeof(std::uintptr_t))) {
                REX::ERROR("[jump] galaxy selection Address Library bindings resolve outside Starfield.exe; bridge disabled");
                return false;
            }
            if (std::memcmp(reinterpret_cast<const void*>(selectAddress),
                    kSelectGalaxySystem116244Prologue.data(),
                    kSelectGalaxySystem116244Prologue.size()) != 0) {
                REX::ERROR("[jump] Address Library ID 94292 failed the Starfield 1.16.244 prologue fingerprint at {:016X}; bridge disabled",
                    selectAddress);
                return false;
            }
            if (std::memcmp(reinterpret_cast<const void*>(closeAddress),
                    kCloseGalaxyQuickSelect116244Prologue.data(),
                    kCloseGalaxyQuickSelect116244Prologue.size()) != 0) {
                REX::ERROR("[jump] Address Library ID 94308 failed the Starfield 1.16.244 prologue fingerprint at {:016X}; bridge disabled",
                    closeAddress);
                return false;
            }

            g_selectGalaxySystem.store(
                reinterpret_cast<SelectGalaxySystem_t>(selectAddress),
                std::memory_order_release);
            g_closeGalaxyQuickSelect.store(
                reinterpret_cast<CloseGalaxyQuickSelect_t>(closeAddress),
                std::memory_order_release);
            REX::INFO("[jump] native galaxy selection bindings validated: select ID 94292 RVA=0x{:X}, Quick Select close ID 94308 RVA=0x{:X}, menuVtableID=446845, galaxyVtableID=446425",
                selectAddress - module, closeAddress - module);
            return true;
        }

        bool IsShipInSpace(RE::TESObjectREFR* a_ship)
        {
            const auto predicate = g_isInSpace.load(std::memory_order_acquire);
            return a_ship && predicate && predicate(a_ship, false);
        }

        bool IsFlying()
        {
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            return IsShipInSpace(ship);
        }

        struct LiveReferenceTarget
        {
            std::uint32_t referenceFormID{ 0 };
            std::uint32_t baseFormID{ 0 };
        };

        std::vector<LiveReferenceTarget> ResolveStationTargets(std::uint32_t a_mapFormID)
        {
            std::vector<LiveReferenceTarget> resolved;
            const auto appendLive = [&resolved](LiveReferenceTarget a_candidate) {
                const auto form = RE::TESForm::LookupByID(a_candidate.referenceFormID);
                const auto reference = form ? form->As<RE::TESObjectREFR>() : nullptr;
                const auto base = reference ? reference->GetBaseObject() : nullptr;
                if (!base || !BodyIndex::IsStationBase(base->GetFormID()))
                    return;
                a_candidate.baseFormID = base->GetFormID();
                resolved.push_back(std::move(a_candidate));
            };

            // Dynamic map markers may already be the live station reference.
            if (const auto form = RE::TESForm::LookupByID(a_mapFormID)) {
                if (const auto reference = form->As<RE::TESObjectREFR>()) {
                    const auto base = reference->GetBaseObject();
                    if (base && BodyIndex::IsStationBase(base->GetFormID())) {
                        appendLive({
                            .referenceFormID = a_mapFormID,
                            .baseFormID = base->GetFormID(),
                        });
                    }
                }
            }
            for (const auto& candidate : BodyIndex::StationTargets(a_mapFormID))
                appendLive({ candidate.referenceFormID, candidate.baseFormID });

            std::ranges::sort(resolved, {}, &LiveReferenceTarget::referenceFormID);
            resolved.erase(std::unique(resolved.begin(), resolved.end(),
                [](const LiveReferenceTarget& a_left,
                    const LiveReferenceTarget& a_right) {
                    return a_left.referenceFormID == a_right.referenceFormID;
                }), resolved.end());
            return resolved;
        }

        std::vector<HudRow> CurrentHudTargets(std::uint32_t a_formID)
        {
            std::vector<HudRow> matches;
            std::lock_guard lock{ g_hudRowsMutex };
            for (const auto& row : g_hudRows) {
                if (row.id == a_formID)
                    matches.push_back(row);
            }
            return matches;
        }

        ProcessedHudSnapshot CurrentProcessedHudSnapshot()
        {
            std::lock_guard lock{ g_processedHudMutex };
            return g_processedHudSnapshot;
        }

        bool AssignNativeShipTarget(const BodyDestination& a_destination)
        {
            if (a_destination.kind != BodyKind::kStation)
                return true;

            const auto form = RE::TESForm::LookupByID(a_destination.formID);
            const auto reference = form ? form->As<RE::TESObjectREFR>() : nullptr;
            const auto base = reference ? reference->GetBaseObject() : nullptr;
            const auto setter = g_setShipHudTarget.load(std::memory_order_acquire);
            const bool exactBase = base &&
                (!a_destination.targetBaseFormID ||
                    base->GetFormID() == a_destination.targetBaseFormID);
            const bool validStation = exactBase &&
                a_destination.kind == BodyKind::kStation &&
                BodyIndex::IsStationBase(base->GetFormID());
            if (!setter || !validStation) {
                REX::ERROR("[target] refusing native assignment for {:08X}: live {} REFR validation failed",
                    a_destination.formID, DestinationKindName(a_destination.kind));
                return false;
            }

            setter(a_destination.formID);
            REL::Relocation<std::uint32_t*> current{ kCurrentShipHudTarget };
            std::uint32_t observed = 0;
            if (!ReadMemory(current.address(), observed) || observed != a_destination.formID) {
                REX::ERROR("[target] native assignment of {:08X} did not commit (observed {:08X})",
                    a_destination.formID, observed);
                return false;
            }

            REX::INFO("[target] native cockpit target assigned: map={:08X}/{} ref={:08X} base={:08X}",
                a_destination.mapFormID, a_destination.mapType, a_destination.formID,
                base->GetFormID());
            return true;
        }

        std::optional<BodyDestination> Destination()
        {
            std::lock_guard lock{ g_destinationMutex };
            return g_destination;
        }

        std::optional<RemoteMoonContinuation> RemoteMoonState()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            if (g_remoteMoonContinuation.phase == RemoteMoonPhase::kNone)
                return std::nullopt;
            return g_remoteMoonContinuation;
        }

        bool RemoteMoonContinuationActive()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            return g_remoteMoonContinuation.phase != RemoteMoonPhase::kNone;
        }

        void ResetRemoteMoonContinuation()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            g_remoteMoonContinuation = {};
        }

        void CancelOrReleaseHudCruiseInput(const char* a_reason)
        {
            const char* action = nullptr;
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
                if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressPending) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                    g_hudCruiseUserEvent = "Cruise";
                    action = "cancelled pending press";
                } else if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressed) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kReleasePending;
                    action = "queued release";
                }
            }
            g_hudUiDirty.store(true, std::memory_order_release);
            if (action && Settings::Verbose())
                REX::INFO("[input] HUD Cruise {}: {}", action, a_reason);
        }

        bool QueueHudCruisePress(RE::InputEvent::DeviceType a_device)
        {
            std::lock_guard lock{ g_hudCruiseInputMutex };
            if (g_hudCruiseInputPhase != HudCruiseInputPhase::kIdle)
                return false;
            // ShipReticle installs a different quick/hold combo for controller
            // mode. Both combos reach the same stock Cruise hold callback.
            g_hudCruiseUserEvent = a_device == RE::InputEvent::DeviceType::kGamepad ?
                                       kCruiseMapGamepadUserEvent :
                                       "Cruise";
            g_hudCruiseInputPhase = HudCruiseInputPhase::kPressPending;
            // The Starmap's completed fill is the user's confirmation. Keep
            // the separate cockpit hold pressed even if the physical key is
            // released, then release on Cruise activation or the safety limit.
            g_hudCruiseInputLatched = true;
            g_hudCruiseInputStarted = Clock::now();
            g_hudUiDirty.store(true, std::memory_order_release);
            return true;
        }

        bool HudCruiseInputLatched()
        {
            std::lock_guard lock{ g_hudCruiseInputMutex };
            return g_hudCruiseInputLatched;
        }

        void ResetHold(const char* a_reason)
        {
            bool changed = false;
            {
                std::lock_guard lock{ g_holdMutex };
                changed = g_hold.active || g_claimMapKey;
                g_hold = {};
                g_claimMapKey = false;
            }
            CancelOrReleaseHudCruiseInput(a_reason);
            const auto state = g_state.load(std::memory_order_acquire);
            if (state == NavState::kAwaitingCruise)
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            else if (state == NavState::kMapSelection && Settings::Verbose())
                REX::INFO("[input] active Starmap selection preserved across hold reset: {}",
                    a_reason);
            if (changed && Settings::Verbose())
                REX::INFO("[input] pending physical hold reset: {}", a_reason);
        }

        void ClearDestination(const char* a_reason)
        {
            std::optional<BodyDestination> old;
            {
                std::lock_guard lock{ g_destinationMutex };
                old = std::move(g_destination);
                g_destination.reset();
            }
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            {
                std::lock_guard lock{ g_remoteRouteMutex };
                g_remoteRouteRequest = {};
            }
            ResetRemoteMoonContinuation();
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_state.store(NavState::kIdle, std::memory_order_release);
            g_markedDistance.store(-1.0, std::memory_order_release);
            g_courseWasLocked.store(false, std::memory_order_release);
            g_arrivalCheckID.store(0, std::memory_order_release);
            g_pendingJumpDevice.store(RE::InputEvent::DeviceType::kNone,
                std::memory_order_release);
            g_pendingStationResolveTicks.store(0, std::memory_order_release);
            g_pendingStationAssignedID.store(0, std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
            if (old)
                REX::INFO("[destination] cleared {:08X} '{}': {}", old->formID,
                    old->localizedName, a_reason);
        }

        void StoreDestination(BodyDestination a_destination)
        {
            std::optional<BodyDestination> old;
            {
                std::lock_guard lock{ g_destinationMutex };
                old = g_destination;
                g_destination = a_destination;
            }
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            ResetRemoteMoonContinuation();
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_state.store(NavState::kMapSelection, std::memory_order_release);
            g_markedDistance.store(-1.0, std::memory_order_release);
            g_courseWasLocked.store(false, std::memory_order_release);
            g_arrivalCheckID.store(0, std::memory_order_release);
            g_pendingJumpDevice.store(RE::InputEvent::DeviceType::kNone,
                std::memory_order_release);
            g_pendingStationResolveTicks.store(0, std::memory_order_release);
            g_pendingStationAssignedID.store(0, std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
            if (old && old->formID != a_destination.formID)
                REX::INFO("[destination] replaced {:08X} '{}' with {:08X} '{}'",
                    old->formID, old->localizedName, a_destination.formID,
                    a_destination.localizedName);
            else
                REX::INFO("[destination] marked {:08X} '{}' (course={:08X} system={} parent={} planet={} kind={})",
                    a_destination.formID, a_destination.localizedName,
                    CourseTargetID(a_destination),
                    a_destination.galaxy.system, a_destination.galaxy.parent,
                    a_destination.galaxy.planet,
                    DestinationKindName(a_destination.kind));
        }

        bool RemoteStationContinuationActive()
        {
            return g_pendingStationAssignedID.load(std::memory_order_acquire) != 0;
        }

        void FailRemoteStationContinuation(const char* a_reason)
        {
            if (!RemoteStationContinuationActive())
                return;
            REX::WARN("[station] automatic remote continuation failed closed: {}",
                a_reason);
            CancelOrReleaseHudCruiseInput(a_reason);
            ClearDestination(a_reason);
        }

        class LoadGameSink final : public RE::BSTEventSink<RE::TESLoadGameEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&,
                RE::BSTEventSource<RE::TESLoadGameEvent>*) override
            {
                // Event delivery is not assumed to be the main game thread.
                // Publish only a value signal; the verified BSService frame
                // owns the actual navigation/input reset.
                g_loadClearPending.store(true, std::memory_order_release);
                REX::INFO("[safety] TESLoadGameEvent received; queued destination clear");
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_loadGameSink;

        void TryInstallLoadGameSink()
        {
            if (g_loadGameSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto function = kLoadGameGetEventSource.address();
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            const bool readable = ReadMemory(function, prologue);
            const bool prologueMatches = readable &&
                prologue == kGlobalEventGetEventSource116244Prologue;
            if (!prologueMatches) {
                REX::ERROR("[safety] TESLoadGameEvent ID 64149 fingerprint failed at {:016X}: [{}]; remote targets disabled",
                    function, readable ? HexBytes(prologue) : "unreadable");
                return;
            }

            const auto source = RE::TESLoadGameEvent::GetEventSource();
            std::uintptr_t vtable = 0;
            const bool sourceReadable = source &&
                ReadMemory(reinterpret_cast<std::uintptr_t>(source), vtable);
            const auto expectedSource = kLoadGameSourceStatic.address();
            const auto expectedVtable = kLoadGameSourceVtable.address();
            const bool sourceMatches = reinterpret_cast<std::uintptr_t>(source) == expectedSource;
            const bool vtableMatches = sourceReadable && vtable == expectedVtable;
            REX::INFO("[safety] TESLoadGameEvent guard prologue=[{}] source={:016X}/{:016X} match={} vtable={:016X}/{:016X} match={}",
                HexBytes(prologue), reinterpret_cast<std::uintptr_t>(source), expectedSource,
                sourceMatches, vtable, expectedVtable, vtableMatches);
            if (!source || !sourceMatches || !vtableMatches) {
                REX::ERROR("[safety] TESLoadGameEvent identity guard failed; remote targets disabled");
                return;
            }

            source->RegisterSink(&g_loadGameSink);
            g_loadGameSinkReady.store(true, std::memory_order_release);
            REX::INFO("[safety] TESLoadGameEvent sink registered; jump-persistent remote targets enabled");
        }

        class GravJumpSink final : public RE::BSTEventSink<RE::Spaceship::GravJumpEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::Spaceship::GravJumpEvent& a_event,
                RE::BSTEventSource<RE::Spaceship::GravJumpEvent>*) override
            {
                const auto player = RE::PlayerCharacter::GetSingleton();
                if (!player || !a_event.ship || a_event.ship.get() != player->GetSpaceship())
                    return RE::BSEventNotifyControl::kContinue;

                const auto retained = Destination();
                REX::INFO("[jump] player grav-jump state={} destination={:08X} navState={} retainedTarget={:08X}",
                    a_event.state,
                    a_event.destination ? a_event.destination->GetFormID() : 0,
                    static_cast<std::uint32_t>(g_state.load(std::memory_order_acquire)),
                    retained ? retained->formID : 0);
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_gravJumpSink;

        void TryInstallGravJumpSink()
        {
            if (g_gravJumpSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto function = kGravJumpGetEventSource.address();
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            const bool readable = ReadMemory(function, prologue);
            const bool prologueMatches = readable &&
                prologue == kGlobalEventGetEventSource116244Prologue;
            const auto source = prologueMatches ?
                RE::Spaceship::GravJumpEvent::GetEventSource() : nullptr;
            std::uintptr_t vtable = 0;
            const bool sourceReadable = source &&
                ReadMemory(reinterpret_cast<std::uintptr_t>(source), vtable);
            const auto expectedVtable = kGravJumpSourceVtable.address();
            const bool vtableMatches = sourceReadable && vtable == expectedVtable;
            REX::INFO("[jump] GravJumpEvent guard prologue=[{}] source={:016X} vtable={:016X}/{:016X} match={}",
                readable ? HexBytes(prologue) : "unreadable",
                reinterpret_cast<std::uintptr_t>(source), vtable, expectedVtable,
                prologueMatches && vtableMatches);
            if (!source || !prologueMatches || !vtableMatches) {
                REX::WARN("[jump] GravJumpEvent identity guard failed; jump acknowledgement diagnostics unavailable");
                return;
            }

            source->RegisterSink(&g_gravJumpSink);
            REX::INFO("[jump] player-filtered GravJumpEvent acknowledgement sink registered");
        }

        MapEligibility EvaluateMapSelection(MapSnapshot a_snapshot)
        {
            const auto unavailable = [](EligibilityCode a_code, std::string a_label,
                                         std::string a_detail) {
                return MapEligibility{
                    .code = a_code,
                    .show = true,
                    .enabled = false,
                    .label = std::move(a_label),
                    .detail = std::move(a_detail),
                };
            };

            if (!a_snapshot.openedWhileFlying || a_snapshot.view != kSystemView ||
                a_snapshot.session == 0 ||
                a_snapshot.session != g_mapSession.load(std::memory_order_acquire) ||
                a_snapshot.generation != g_mapMovie.generation.load(std::memory_order_acquire)) {
                return {
                    .code = EligibilityCode::kHidden,
                    .detail = "not an active-flight system-view map session",
                };
            }
            const bool usingGamepad = g_lastInputWasGamepad.load(std::memory_order_acquire);
            const bool cruiseControlBound = usingGamepad ?
                g_cruiseMapGamepadButton.load(std::memory_order_acquire) >= 0 :
                g_cruiseMapKey.load(std::memory_order_acquire) >= 0 ||
                    g_cruiseMapMouseButton.load(std::memory_order_acquire) >= 0;
            if (!cruiseControlBound)
                return unavailable(EligibilityCode::kCruiseControlUnbound,
                    "CRUISE CONTROL IS NOT BOUND",
                    usingGamepad ? "SHMonocle has no controller binding" :
                                   "Cruise has no keyboard or mouse binding");
            if (!a_snapshot.haveCapturedSystem)
                return unavailable(EligibilityCode::kCurrentSystemUnavailable,
                    "CURRENT SYSTEM UNAVAILABLE",
                    "cockpit current system is not resolved for this map session");
            if (a_snapshot.highlightedMarkerCount == 0)
                return unavailable(EligibilityCode::kSelectBody,
                    "HIGHLIGHT A DESTINATION",
                    "system view has no highlight-radius target marker");
            if (a_snapshot.highlightedMarkerCount != 1)
                return unavailable(EligibilityCode::kAmbiguousTarget,
                    "TARGET IS AMBIGUOUS",
                    std::format("system view has {} highlight-radius marker candidates",
                        a_snapshot.highlightedMarkerCount));
            if (a_snapshot.markerBodyID == 0) {
                return unavailable(EligibilityCode::kTargetTypeUnsupported,
                    "TARGET HAS NO CRUISE ID",
                    std::format("highlight-radius marker has type {} but no id",
                        a_snapshot.markerBodyType));
            }

            const bool planetary = a_snapshot.markerBodyType == kPlanetType ||
                a_snapshot.markerBodyType == kMoonType;
            if (!BodyIndex::Ready())
                return unavailable(EligibilityCode::kTargetDataLoading,
                    "CRUISE TARGET DATA LOADING",
                    "PNDT/GNAM and starstation reference index is not ready");
            if (!planetary) {
                const auto browsedSystemID = MapTreeSystemID(a_snapshot.treeBodyID);
                if (browsedSystemID && *browsedSystemID != a_snapshot.capturedSystem) {
                    if (g_cruiseActive.load(std::memory_order_acquire)) {
                        return unavailable(EligibilityCode::kCruiseActive,
                            "EXIT CRUISE FIRST",
                            "vanilla cannot execute a grav-jump route while Cruise is active, and the stock HUD Cruise control is not handled while the Starmap is open");
                    }
                    auto indexedStations =
                        BodyIndex::StationTargets(a_snapshot.markerBodyID);
                    indexedStations.erase(std::remove_if(indexedStations.begin(),
                        indexedStations.end(), [](const BodyIndex::StationTarget& a_target) {
                            return !a_target.referenceFormID ||
                                   !a_target.courseFormID ||
                                   !BodyIndex::IsStationBase(a_target.baseFormID);
                        }), indexedStations.end());
                    std::ranges::sort(indexedStations, {},
                        &BodyIndex::StationTarget::referenceFormID);
                    indexedStations.erase(std::unique(indexedStations.begin(),
                        indexedStations.end(),
                        [](const BodyIndex::StationTarget& a_left,
                            const BodyIndex::StationTarget& a_right) {
                            return a_left.referenceFormID == a_right.referenceFormID;
                        }), indexedStations.end());
                    if (indexedStations.size() > 1) {
                        return unavailable(EligibilityCode::kAmbiguousTarget,
                            "STATION TARGET IS AMBIGUOUS",
                            std::format("remote station CELL {:08X}/{} has {} exact indexed references",
                                a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                                indexedStations.size()));
                    }
                    if (indexedStations.size() == 1) {
                        if (!g_loadGameSinkReady.load(std::memory_order_acquire)) {
                            return unavailable(EligibilityCode::kRemoteSafetyUnavailable,
                                "REMOTE CRUISE SAFETY UNAVAILABLE",
                                "guarded TESLoadGameEvent sink is unavailable; refusing a remote station mark that could survive a save load");
                        }
                        const auto& station = indexedStations.front();
                        auto destination = BodyDestination{
                            .kind = BodyKind::kStation,
                            .formID = station.referenceFormID,
                            .targetBaseFormID = station.baseFormID,
                            .courseFormID = station.courseFormID,
                            .mapFormID = a_snapshot.markerBodyID,
                            .mapType = a_snapshot.markerBodyType,
                            .galaxy = { .system = *browsedSystemID },
                            .localizedName = a_snapshot.markerName.empty() ?
                                (station.editorID.empty() ?
                                        std::format("STATION {:08X}", station.referenceFormID) :
                                        station.editorID) :
                                a_snapshot.markerName,
                            .menuGeneration = a_snapshot.generation,
                        };
                        return {
                            .code = EligibilityCode::kEligible,
                            .show = true,
                            .enabled = true,
                            .label = kRemoteCruiseMapActionLabel,
                            .detail = std::format("eligible remote station CELL={:08X}/{} indexedRef={:08X} base={:08X} courseMarker={:08X} '{}' system={}",
                                destination.mapFormID, destination.mapType,
                                destination.formID, station.baseFormID,
                                station.courseFormID, destination.localizedName,
                                destination.galaxy.system),
                            .destination = std::move(destination),
                        };
                    }
                    return {
                        .code = EligibilityCode::kHidden,
                        .detail = std::format("remote non-station marker {:08X}/{} has no stable unloaded target identity",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType),
                    };
                }

                const auto stationTargets = ResolveStationTargets(a_snapshot.markerBodyID);
                if (stationTargets.size() > 1)
                    return unavailable(EligibilityCode::kAmbiguousTarget,
                        "STATION TARGET IS AMBIGUOUS",
                        std::format("non-planet marker {:08X}/{} resolves to {} live starstation references",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                            stationTargets.size()));

                if (!stationTargets.empty()) {
                    const auto& station = stationTargets.front();
                    auto destination = BodyDestination{
                        .kind = BodyKind::kStation,
                        .formID = station.referenceFormID,
                        .targetBaseFormID = station.baseFormID,
                        .mapFormID = a_snapshot.markerBodyID,
                        .mapType = a_snapshot.markerBodyType,
                        .galaxy = { .system = a_snapshot.capturedSystem },
                        .localizedName = a_snapshot.markerName.empty() ?
                            std::format("STATION {:08X}", station.referenceFormID) :
                            a_snapshot.markerName,
                        .menuGeneration = a_snapshot.generation,
                    };
                    return {
                        .code = EligibilityCode::kEligible,
                        .show = true,
                        .enabled = true,
                        .label = kCruiseMapActionLabel,
                        .detail = std::format("eligible station map={:08X}/{} ref={:08X} base={:08X} '{}'",
                            destination.mapFormID, destination.mapType,
                            destination.formID, station.baseFormID,
                            destination.localizedName),
                        .destination = std::move(destination),
                    };
                }

                return {
                    .code = EligibilityCode::kHidden,
                    .detail = std::format("unsupported non-station marker {:08X}/{} is vanilla-owned",
                        a_snapshot.markerBodyID, a_snapshot.markerBodyType),
                };
            }

            if (a_snapshot.dossierBodyID == 0 ||
                (a_snapshot.dossierBodyType != kPlanetType &&
                    a_snapshot.dossierBodyType != kMoonType) ||
                a_snapshot.markerBodyID != a_snapshot.dossierBodyID ||
                a_snapshot.markerBodyType != a_snapshot.dossierBodyType) {
                return unavailable(EligibilityCode::kTargetDataUpdating,
                    "TARGET DATA IS UPDATING",
                    std::format("marker {:08X}/{} differs from dossier {:08X}/{}",
                        a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                        a_snapshot.dossierBodyID, a_snapshot.dossierBodyType));
            }

            // Live 1.16.244 proof identifies the selected system-view body as
            // the one StarMapMenuMarkersData row with bIsInHighlightRadius.
            // Tree focus remains the system/star and does not join identity.
            const auto form = RE::TESForm::LookupByID(a_snapshot.dossierBodyID);
            if (!form || form->GetFormType() != RE::FormType::kPNDT) {
                return unavailable(EligibilityCode::kTargetTypeUnsupported,
                    "TARGET TYPE IS NOT SUPPORTED",
                    std::format("dossier {:08X} is not a live PNDT form",
                        a_snapshot.dossierBodyID));
            }
            const auto body = BodyIndex::Lookup(a_snapshot.dossierBodyID);
            if (!body) {
                return unavailable(EligibilityCode::kTargetNotIndexed,
                    "TARGET DATA IS NOT AVAILABLE",
                    std::format("dossier PNDT {:08X} has no parsed GNAM identity",
                        a_snapshot.dossierBodyID));
            }
            const bool remote = body->galaxy.system != a_snapshot.capturedSystem;
            if (remote && g_cruiseActive.load(std::memory_order_acquire)) {
                return unavailable(EligibilityCode::kCruiseActive,
                    "EXIT CRUISE FIRST",
                    "vanilla cannot execute a grav-jump route while Cruise is active, and the stock HUD Cruise control is not handled while the Starmap is open");
            }
            if (remote && !g_loadGameSinkReady.load(std::memory_order_acquire))
                return unavailable(EligibilityCode::kRemoteSafetyUnavailable,
                    "REMOTE CRUISE SAFETY UNAVAILABLE",
                    "guarded TESLoadGameEvent sink is unavailable; refusing a mark that could survive a save load");

            auto destination = BodyDestination{
                .kind = a_snapshot.dossierBodyType == kMoonType ? BodyKind::kMoon : BodyKind::kPlanet,
                .formID = a_snapshot.dossierBodyID,
                .mapFormID = a_snapshot.markerBodyID,
                .mapType = a_snapshot.dossierBodyType,
                .galaxy = body->galaxy,
                .localizedName = a_snapshot.dossierName.empty() ?
                    a_snapshot.markerName : a_snapshot.dossierName,
                .menuGeneration = a_snapshot.generation,
            };
            return {
                .code = EligibilityCode::kEligible,
                .show = true,
                .enabled = true,
                .label = remote ? kRemoteCruiseMapActionLabel : kCruiseMapActionLabel,
                .detail = std::format("eligible {}{} {:08X} '{}' system={}",
                    remote ? "remote " : "",
                    DestinationKindName(destination.kind),
                    destination.formID, destination.localizedName,
                    destination.galaxy.system),
                .destination = std::move(destination),
            };
        }
