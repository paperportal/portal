#pragma once

#include <stdint.h>

#include "services/devserver_service.h"

enum class HostEventType : uint8_t {
    Tick = 0,
    Gesture = 1,
    HttpRequest = 2,
    WifiEvent = 3,
    DevCommand = 4,
};

struct HostEventGesture {
    int32_t kind;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    int32_t duration_ms;
    int32_t flags;
};

struct HostEventHttpRequest {
    int32_t req_id;
    int32_t method;
    int32_t content_len;
};

struct HostEventWifiEvent {
    int32_t kind;
    int32_t arg0;
    int32_t arg1;
};

struct HostEventDevCommand {
    devserver::DevCommand *cmd;
};

struct HostEvent {
    HostEventType type;
    int32_t now_ms;
    union {
        HostEventGesture gesture;
        HostEventHttpRequest http;
        HostEventWifiEvent wifi;
        HostEventDevCommand dev;
    } data;
};

inline HostEvent MakeTickEvent(int32_t now_ms)
{
    HostEvent ev{};
    ev.type = HostEventType::Tick;
    ev.now_ms = now_ms;
    return ev;
}

inline HostEvent MakeGestureEvent(int32_t now_ms, const HostEventGesture &gesture)
{
    HostEvent ev{};
    ev.type = HostEventType::Gesture;
    ev.now_ms = now_ms;
    ev.data.gesture = gesture;
    return ev;
}

inline HostEvent MakeHttpRequestEvent(int32_t now_ms, int32_t req_id, int32_t method, int32_t content_len)
{
    HostEvent ev{};
    ev.type = HostEventType::HttpRequest;
    ev.now_ms = now_ms;
    ev.data.http.req_id = req_id;
    ev.data.http.method = method;
    ev.data.http.content_len = content_len;
    return ev;
}

inline HostEvent MakeWifiEvent(int32_t now_ms, int32_t kind, int32_t arg0, int32_t arg1)
{
    HostEvent ev{};
    ev.type = HostEventType::WifiEvent;
    ev.now_ms = now_ms;
    ev.data.wifi.kind = kind;
    ev.data.wifi.arg0 = arg0;
    ev.data.wifi.arg1 = arg1;
    return ev;
}

inline HostEvent MakeDevCommandEvent(int32_t now_ms, devserver::DevCommand *cmd)
{
    HostEvent ev{};
    ev.type = HostEventType::DevCommand;
    ev.now_ms = now_ms;
    ev.data.dev.cmd = cmd;
    return ev;
}
