const sdk = @import("paper_portal_sdk");

const core = sdk.core;
const microtask = sdk.microtask;
const net = sdk.net;
const socket = sdk.socket;
const tls_socket = sdk.tls_socket;
const Error = tls_socket.Error;

const kPort: u16 = 8443;

// Self-signed development certificate/key generated via:
//   openssl req -x509 -newkey rsa:2048 -nodes \
//     -keyout server.key.pem -out server.crt.pem \
//     -days 365 -subj "/CN=paperportal"
//
// Security note: This is for development/testing only.
const kServerCertPem =
    \\-----BEGIN CERTIFICATE-----
    \\MIIDDTCCAfWgAwIBAgIUWRh4KwwKfpUe3JfNjbguu3xZk+YwDQYJKoZIhvcNAQEL
    \\BQAwFjEUMBIGA1UEAwwLcGFwZXJwb3J0YWwwHhcNMjYwMjI2MjI0OTIzWhcNMjcw
    \\MjI2MjI0OTIzWjAWMRQwEgYDVQQDDAtwYXBlcnBvcnRhbDCCASIwDQYJKoZIhvcN
    \\AQEBBQADggEPADCCAQoCggEBANt5/fZ6YUhXzqcrWV8bwp5xdYZLDbOvFJrX08Be
    \\KAklstA63H98S/LQy2MU3p0jZtkKXug49PrxxpwJfz6j0PS6xmVRNBn0KLhe4rDl
    \\KTqmxCkAAnvfcZ83YVq2LUpfhJ3rjfo01QWsPByli608wjxudgorSm7tb/Cnodij
    \\GHRxb7WoGodnLxvIvHCjqcIJbPSqvtQksxJhe3xRy0OoInQcJ34hKd8FBYR81H1q
    \\AFgyyl3V1KR3DGALLCTb1Q7TM6vE79oD8WiqB3007xug1vBiSMah68pDrRP4SZkU
    \\GXxlbwgujSWGmV3+t6iyhwD/LDRbQ5GRmETa5Mzigfvm7k8CAwEAAaNTMFEwHQYD
    \\VR0OBBYEFJyDrA27UVFIMLsksRKgQ8TimXnkMB8GA1UdIwQYMBaAFJyDrA27UVFI
    \\MLsksRKgQ8TimXnkMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEB
    \\ALCmUAYOCzcDVgOXz0qO4Jqm3IRTEh8YMrPc9DkEFy9Xlwtj534wjSvpCT/huQgh
    \\qsZP0ZFQibOLULnOoScnAopyZzL0By4ovew1/10v6BT7pzYhgduLKBI9MtJKGNdu
    \\vnUiqyeyMLjWpU4MYzcnvFe7LFcZZMv2yNxBCUqE/vbcqyGnlS/HV0/f77TSB1Vd
    \\tA+206htxu0qxtGCdOrmfVrKgK6s9cQaFf5wvkxbEqHtNNXiDdiI7v/ZZBIdhTNV
    \\Gt7Sc/DX7GYddjVdX1s0glbfO/EOipgHR0VqbY/Wf+owqYQuzYvbYHkDNGUfArIV
    \\Q3y9M+DcQ948GUjm2uAOrXw=
    \\-----END CERTIFICATE-----
;

const kServerKeyPem =
    \\-----BEGIN PRIVATE KEY-----
    \\MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDbef32emFIV86n
    \\K1lfG8KecXWGSw2zrxSa19PAXigJJbLQOtx/fEvy0MtjFN6dI2bZCl7oOPT68cac
    \\CX8+o9D0usZlUTQZ9Ci4XuKw5Sk6psQpAAJ733GfN2Fati1KX4Sd6436NNUFrDwc
    \\pYutPMI8bnYKK0pu7W/wp6HYoxh0cW+1qBqHZy8byLxwo6nCCWz0qr7UJLMSYXt8
    \\UctDqCJ0HCd+ISnfBQWEfNR9agBYMspd1dSkdwxgCywk29UO0zOrxO/aA/Foqgd9
    \\NO8boNbwYkjGoevKQ60T+EmZFBl8ZW8ILo0lhpld/reosocA/yw0W0ORkZhE2uTM
    \\4oH75u5PAgMBAAECggEAMycXIFu8kbpZYU/yp/6V2QzTmeWjP2ZGdKJp6XNofF1L
    \\dEnLu3DSWLj+Nk0WZ2RpY5uNrC0eN+Ci7BowjH5sxKVFeTa4YHYuCW6gAh+fANr+
    \\bBni+lEFL9kd+SDtpGBnzdCpShAHOCfA6OJSkCFRmfkazNrzIbB4Pwwq5g15NSYO
    \\hqi7U5lCUiodLybpVOeL9ZYAHDTBz8VNSqib8tqlaoi+3fXjKa+ifKQlGVM7Eto7
    \\IGDbc4EoDThU4mAZddjLL4ITLQZIpWUVZpBQJCR67grBJxi18cLTjbRqepHy1WL0
    \\tPbp/gmFXRbGK1Ow4fvQ16WX3VgSQT0Bf2A6eOiORQKBgQD4x8xbhK/qIrxEEEoz
    \\VhQkx/nS7JEcTemfBtw3lZExpgKD4XKJjPURCSTw/2ONjQzUBjzuWXkUsgsE/Ev0
    \\4u7dgkuJgCSGkualV+MYZbAyNfC1A2FCrcVJiAHBlBah5NSgooFmhtvrg9NhUJXc
    \\s3I9Zu2s1nC9R+SF0VAyfIyZFQKBgQDh2H5VsaE5GYWwbPOLqN/Oa2yfEs+hleYU
    \\/acCMiGhBCpCHau/9k4QhD/AEhO/x25McdOt+1arLCvpVftVvC67xcgX1l3K/gZx
    \\gx8gxhHG1Z3pk4F4fbr++D3CEV5Zhwbv72heY64IYz8mz64LxqNNSY/SIRr6ue9Y
    \\OvzH3X460wKBgDbOnYDT/IVSXbOI+y/QGOP5glorb9SSfZNhSYadVr1fW1J+imMv
    \\QJYeM5s8SExY9KiJL0c7QueCSdcxVfJ/RCxclhOWvXVAS7vFXuxzCpwlMgBO3FB6
    \\Q+ptIiXpYAsOhKFW67cQosaB5kGcJUfX6KUKVfYrz+6DNh8c+9QDiF35AoGBAMah
    \\yjtERzAZKmPI70rd9DlwcdBWkxbi58F+deyQetNK/n+36N6kmnQcXJVpUMVTecby
    \\NeLHM6rI5rYKUUtOfyvW/+03NLbcRH6BfFcu3WOrbX+JpE19B4JIgsi0ze6fu5I7
    \\Vefuc5oc7+YKsmDZC7dmA4Akbb4m2kMXXkvKmsLRAoGBAKjL9Pi3bhXBj4y/EC0+
    \\W547Wyew3Gs8pOjTCXxd0QfhcdtEAGuHJEj7K1OxXVJCMA3Pti4is52KfDGi90Hp
    \\ANQD1aPY0Kn4kgPUgjpRuG/v1zJ5EIYPVXo8s+Qonv9GFHcL7pcRHYRFsiDqH2LJ
    \\2i7nqxGwq1ymoYAvaZ8YcM/S
    \\-----END PRIVATE KEY-----
;

const ServerState = struct {
    listener: ?socket.Socket = null,
    config: ?tls_socket.ServerConfig = null,
    client: ?tls_socket.TlsSocket = null,

    pending: [512]u8 = [_]u8{0} ** 512,
    pending_len: usize = 0,
    pending_off: usize = 0,

    fn clearPending(self: *ServerState) void {
        self.pending_len = 0;
        self.pending_off = 0;
    }

    fn closeClient(self: *ServerState) void {
        if (self.client) |*c| c.close() catch {};
        self.client = null;
        self.clearPending();
    }

    pub fn step(self: *ServerState, now_ms: u32) anyerror!microtask.Action {
        _ = now_ms;

        const listener = if (self.listener) |*s| s else return microtask.Action.sleepMs(250);
        const config = if (self.config) |*c| c else return microtask.Action.sleepMs(250);

        if (self.client == null) {
            const accepted = tls_socket.accept(config, listener, 0) catch |err| switch (err) {
                Error.NotReady => return microtask.Action.sleepMs(50),
                else => {
                    core.log.ferr("tls_echo: accept failed: {s}", .{@errorName(err)});
                    return microtask.Action.sleepMs(250);
                },
            };

            self.client = accepted.socket;
            self.clearPending();

            const a = accepted.addr;
            core.log.finfo("tls_echo: client {d}.{d}.{d}.{d}:{d}", .{ a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port });
            return microtask.Action.yieldSoon();
        }

        const client = &self.client.?;

        // Flush pending bytes first.
        if (self.pending_off < self.pending_len) {
            const chunk = self.pending[self.pending_off..self.pending_len];
            const wrote = client.send(chunk, 0) catch |err| switch (err) {
                Error.NotReady => return microtask.Action.sleepMs(20),
                else => {
                    core.log.ferr("tls_echo: send failed: {s}", .{@errorName(err)});
                    self.closeClient();
                    return microtask.Action.sleepMs(100);
                },
            };
            if (wrote == 0) return microtask.Action.sleepMs(20);
            self.pending_off += wrote;
            if (self.pending_off >= self.pending_len) {
                self.clearPending();
            }
            return microtask.Action.yieldSoon();
        }

        const n = client.recv(self.pending[0..], 0) catch |err| switch (err) {
            Error.NotReady => return microtask.Action.sleepMs(20),
            else => {
                core.log.ferr("tls_echo: recv failed: {s}", .{@errorName(err)});
                self.closeClient();
                return microtask.Action.sleepMs(100);
            },
        };

        if (n == 0) {
            core.log.info("tls_echo: client closed");
            self.closeClient();
            return microtask.Action.sleepMs(50);
        }

        self.pending_len = n;
        self.pending_off = 0;
        return microtask.Action.yieldSoon();
    }
};

var g_initialized: bool = false;
var g_server: ServerState = .{};
var g_task_handle: i32 = 0;

pub fn main() void {
    if (g_initialized) return;
    g_initialized = true;

    core.begin() catch |err| {
        core.log.ferr("main: core.begin failed: {s}", .{@errorName(err)});
        return;
    };

    const features_raw = core.apiFeatures();
    const features: u64 = @bitCast(features_raw);
    core.log.finfo("tls_echo: runtime features=0x{x}", .{features});

    const required = core.Feature.net | core.Feature.socket | core.Feature.socket_tls;
    if ((features & required) != required) {
        core.log.err("tls_echo: missing net/socket/socket_tls host features");
        return;
    }

    if (!net.isReady()) {
        net.connect() catch |err| {
            core.log.ferr("tls_echo: net.connect failed: {s}", .{@errorName(err)});
            return;
        };
    }

    const ip = net.getIpv4() catch .{ 0, 0, 0, 0 };

    var listener = socket.Socket.tcp() catch |err| {
        core.log.ferr("tls_echo: Socket.tcp failed: {s}", .{@errorName(err)});
        return;
    };
    defer listener.close() catch {};

    listener.bind(socket.SocketAddr.any(kPort)) catch |err| {
        core.log.ferr("tls_echo: bind :{d} failed: {s}", .{ kPort, @errorName(err) });
        return;
    };

    listener.listen(4) catch |err| {
        core.log.ferr("tls_echo: listen failed: {s}", .{@errorName(err)});
        return;
    };

    var cfg = tls_socket.ServerConfig.create(kServerCertPem, kServerKeyPem, null, 0) catch |err| {
        core.log.ferr("tls_echo: ServerConfig.create failed: {s}", .{@errorName(err)});
        return;
    };
    defer cfg.deinit() catch {};

    g_server.listener = listener;
    listener.fd = -1;
    g_server.config = cfg;
    cfg.handle = 0;

    g_task_handle = microtask.start(microtask.Task.from(ServerState, &g_server), 0, 0) catch |err| {
        core.log.ferr("tls_echo: microtask.start failed: {s}", .{@errorName(err)});
        g_task_handle = 0;
        ppShutdown();
        return;
    };

    core.log.finfo("tls_echo: listening on {d}.{d}.{d}.{d}:{d} (TLS)", .{ ip[0], ip[1], ip[2], ip[3], kPort });
}

fn cancelHandle(handle: *i32) void {
    if (handle.* <= 0) return;
    microtask.cancel(handle.*) catch {};
    handle.* = 0;
}

pub export fn ppShutdown() void {
    cancelHandle(&g_task_handle);

    g_server.closeClient();
    if (g_server.listener) |*s| s.close() catch {};
    g_server.listener = null;

    if (g_server.config) |*c| c.deinit() catch {};
    g_server.config = null;
}
