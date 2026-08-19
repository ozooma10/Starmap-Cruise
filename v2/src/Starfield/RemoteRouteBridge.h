#pragma once

#include "Map/MapSessionState.h"
#include "Navigation/NavigationRuntime.h"

#include <cstdint>
#include <memory>
#include <string>

class StarfieldBodyResolutionSource;

struct RemoteRouteResult
{
    enum class Kind : std::uint8_t
    {
        None,
        Committed,
        Failed,
    };

    Kind kind {Kind::None};
    OperationId operationId {0};
    MapSessionIdentity source;
    std::string reason;

    explicit operator bool() const
    {
        return kind != Kind::None;
    }
};

class RemoteRouteBridge final
{
public:
    RemoteRouteBridge();
    ~RemoteRouteBridge();

    RemoteRouteBridge(const RemoteRouteBridge&) = delete;
    RemoteRouteBridge(RemoteRouteBridge&&) = delete;
    RemoteRouteBridge& operator=(const RemoteRouteBridge&) = delete;
    RemoteRouteBridge& operator=(RemoteRouteBridge&&) = delete;

    bool Initialize(const StarfieldBodyResolutionSource& bodySource);
    bool Available() const;

    bool Begin(const BeginRemoteRoute& effect, const MapSessionIdentity& activeIdentity);
    void ObserveMapData(const MapSessionIdentity& identity, MapView view, FormID focusedRootId);
    RemoteRouteResult Tick(const MapSessionIdentity& activeIdentity);
    RemoteRouteResult OnMapClosed(const MapSessionIdentity& identity);
    RemoteRouteResult OnMovieCreated(std::uint32_t generation);
    void Cancel();
    bool Active() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
