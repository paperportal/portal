#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "../../sd_card.h"
#include "sdmmc_cmd.h"
#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_fs";

constexpr int kMaxOpenFiles = 16;
constexpr int kMaxOpenDirs = 8;
constexpr size_t kSdInfoSize = 18;

// Handle 0 is reserved as invalid. Valid handles are 1..=N.
int g_file_fds[kMaxOpenFiles] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};
DIR *g_dirs[kMaxOpenDirs] = {nullptr};

bool contains_dotdot_segment(const char *path)
{
    // Reject any ".." segment (bounded by '/' or string boundaries).
    // Examples rejected: "/a/../b", "/..", "/a/.."
    for (const char *p = path; p && *p; p++) {
        if (p[0] != '.') {
            continue;
        }
        if (p[1] != '.') {
            continue;
        }
        const bool left_ok = (p == path) || (p[-1] == '/');
        const bool right_ok = (p[2] == '\0') || (p[2] == '/');
        if (left_ok && right_ok) {
            return true;
        }
    }
    return false;
}

int32_t make_host_path(const char *guest_path, char *out, size_t out_len)
{
    if (!guest_path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs: path is null");
        return kWasmErrInvalidArgument;
    }
    if (!out || out_len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs: out is null/empty");
        return kWasmErrInvalidArgument;
    }

    if (guest_path[0] != '/') {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs: path must start with '/'");
        return kWasmErrInvalidArgument;
    }

    if (contains_dotdot_segment(guest_path)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs: path contains '..'");
        return kWasmErrInvalidArgument;
    }

    const size_t n = strnlen(guest_path, out_len);
    if (n >= out_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs: path too long");
        return kWasmErrInvalidArgument;
    }
    memcpy(out, guest_path, n + 1);
    return kWasmOk;
}

int alloc_file_handle(int fd)
{
    for (int i = 0; i < kMaxOpenFiles; i++) {
        if (g_file_fds[i] < 0) {
            g_file_fds[i] = fd;
            return i + 1;
        }
    }
    return 0;
}

int get_file_fd(int32_t handle)
{
    if (handle <= 0) {
        return -1;
    }
    const int idx = (int)handle - 1;
    if (idx < 0 || idx >= kMaxOpenFiles) {
        return -1;
    }
    return g_file_fds[idx];
}

int32_t close_file_handle(int32_t handle)
{
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_close: invalid handle");
        return kWasmErrInvalidArgument;
    }
    const int idx = (int)handle - 1;
    if (idx < 0 || idx >= kMaxOpenFiles || g_file_fds[idx] < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_close: bad handle");
        return kWasmErrInvalidArgument;
    }

    const int fd = g_file_fds[idx];
    g_file_fds[idx] = -1;
    if (close(fd) != 0) {
        wasm_api_set_last_error(kWasmErrInternal, "fs_close: close failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int alloc_dir_handle(DIR *d)
{
    for (int i = 0; i < kMaxOpenDirs; i++) {
        if (!g_dirs[i]) {
            g_dirs[i] = d;
            return i + 1;
        }
    }
    return 0;
}

DIR *get_dir(int32_t handle)
{
    if (handle <= 0) {
        return nullptr;
    }
    const int idx = (int)handle - 1;
    if (idx < 0 || idx >= kMaxOpenDirs) {
        return nullptr;
    }
    return g_dirs[idx];
}

int32_t close_dir_handle(int32_t handle)
{
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_closedir: invalid handle");
        return kWasmErrInvalidArgument;
    }
    const int idx = (int)handle - 1;
    if (idx < 0 || idx >= kMaxOpenDirs || !g_dirs[idx]) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_closedir: bad handle");
        return kWasmErrInvalidArgument;
    }

    DIR *d = g_dirs[idx];
    g_dirs[idx] = nullptr;
    if (closedir(d) != 0) {
        wasm_api_set_last_error(kWasmErrInternal, "fs_closedir: closedir failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

// Stable, guest-visible flags (do not match host O_* values).
constexpr int32_t kFsRead = 01;
constexpr int32_t kFsWrite = 02;
constexpr int32_t kFsRdwr = 03;
constexpr int32_t kFsCreate = 0100;
constexpr int32_t kFsTrunc = 0200;
constexpr int32_t kFsAppend = 0400;

int32_t translate_open_flags(int32_t guest_flags)
{
    int32_t mode = 0;
    const int32_t acc = guest_flags & 03;
    if (acc == kFsRead) {
        mode |= O_RDONLY;
    }
    else if (acc == kFsWrite) {
        mode |= O_WRONLY;
    }
    else if (acc == kFsRdwr) {
        mode |= O_RDWR;
    }
    else {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_open: invalid access mode");
        return kWasmErrInvalidArgument;
    }

    if (guest_flags & kFsCreate) mode |= O_CREAT;
    if (guest_flags & kFsTrunc) mode |= O_TRUNC;
    if (guest_flags & kFsAppend) mode |= O_APPEND;
    return mode;
}

int32_t fsIsMounted(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return sd_card_is_mounted() ? 1 : 0;
}

int32_t fsMount(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (sd_card_is_mounted()) {
        return kWasmOk;
    }
    if (!sd_card_mount()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_mount: SD mount failed (no card?)");
        return kWasmErrNotReady;
    }
    return kWasmOk;
}

int32_t fsUnmount(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    sd_card_unmount();
    return kWasmOk;
}

int32_t fsOpen(wasm_exec_env_t exec_env, const char *path, int32_t flags)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_open: SD not mounted");
        return kWasmErrNotReady;
    }

    char host_path[256] = "";
    int32_t rc = make_host_path(path, host_path, sizeof(host_path));
    if (rc != kWasmOk) {
        return rc;
    }

    const int32_t host_flags = translate_open_flags(flags);
    if (host_flags < 0) {
        return host_flags;
    }

    const int fd = open(host_path, host_flags, 0666);
    if (fd < 0) {
        const int e = errno;
        if (e == ENOENT) {
            wasm_api_set_last_error(kWasmErrNotFound, "fs_open: not found");
            return kWasmErrNotFound;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_open: open failed");
        return kWasmErrInternal;
    }

    const int handle = alloc_file_handle(fd);
    if (handle == 0) {
        close(fd);
        wasm_api_set_last_error(kWasmErrInternal, "fs_open: too many open files");
        return kWasmErrInternal;
    }

    return (int32_t)handle;
}

int32_t fsClose(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    return close_file_handle(handle);
}

int32_t fsRead(wasm_exec_env_t exec_env, int32_t handle, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_read: out_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_read: out_len < 0");
        return kWasmErrInvalidArgument;
    }

    const int fd = get_file_fd(handle);
    if (fd < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_read: bad handle");
        return kWasmErrInvalidArgument;
    }

    const ssize_t n = read(fd, out_ptr, (size_t)out_len);
    if (n < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "fs_read: read failed");
        return kWasmErrInternal;
    }
    return (int32_t)n;
}

int32_t fsWrite(wasm_exec_env_t exec_env, int32_t handle, const uint8_t *ptr, int32_t len)
{
    (void)exec_env;
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_write: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_write: len < 0");
        return kWasmErrInvalidArgument;
    }

    const int fd = get_file_fd(handle);
    if (fd < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_write: bad handle");
        return kWasmErrInvalidArgument;
    }

    const ssize_t n = write(fd, ptr, (size_t)len);
    if (n < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "fs_write: write failed");
        return kWasmErrInternal;
    }
    return (int32_t)n;
}

int32_t fsSeek(wasm_exec_env_t exec_env, int32_t handle, int32_t offset, int32_t whence)
{
    (void)exec_env;
    const int fd = get_file_fd(handle);
    if (fd < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_seek: bad handle");
        return kWasmErrInvalidArgument;
    }

    int origin = 0;
    if (whence == 0) origin = SEEK_SET;
    else if (whence == 1) origin = SEEK_CUR;
    else if (whence == 2) origin = SEEK_END;
    else {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_seek: invalid whence");
        return kWasmErrInvalidArgument;
    }

    const off_t pos = lseek(fd, (off_t)offset, origin);
    if (pos < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "fs_seek: lseek failed");
        return kWasmErrInternal;
    }
    return (int32_t)pos;
}

int32_t fsStat(wasm_exec_env_t exec_env, const char *path, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_stat: SD not mounted");
        return kWasmErrNotReady;
    }
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_stat: out_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 24) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_stat: out_len too small (need 24)");
        return kWasmErrInvalidArgument;
    }

    char host_path[256] = "";
    int32_t rc = make_host_path(path, host_path, sizeof(host_path));
    if (rc != kWasmOk) {
        return rc;
    }

    struct stat st;
    if (stat(host_path, &st) != 0) {
        const int e = errno;
        if (e == ENOENT) {
            wasm_api_set_last_error(kWasmErrNotFound, "fs_stat: not found");
            return kWasmErrNotFound;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_stat: stat failed");
        return kWasmErrInternal;
    }

    const uint64_t size = (uint64_t)st.st_size;
    const uint8_t is_dir = S_ISDIR(st.st_mode) ? 1 : 0;

    memcpy(out_ptr + 0, &size, 8);
    out_ptr[8] = is_dir;
    out_ptr[9] = 0;
    out_ptr[10] = 0;
    out_ptr[11] = 0;
    out_ptr[12] = 0;
    out_ptr[13] = 0;
    out_ptr[14] = 0;
    out_ptr[15] = 0;
    const int64_t mtime_unix = (int64_t)st.st_mtime;
    memcpy(out_ptr + 16, &mtime_unix, 8);
    return 24;
}

int32_t fsRemove(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_remove: SD not mounted");
        return kWasmErrNotReady;
    }

    char host_path[256] = "";
    int32_t rc = make_host_path(path, host_path, sizeof(host_path));
    if (rc != kWasmOk) {
        return rc;
    }

    if (unlink(host_path) != 0) {
        const int e = errno;
        if (e == ENOENT) {
            wasm_api_set_last_error(kWasmErrNotFound, "fs_remove: not found");
            return kWasmErrNotFound;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_remove: unlink failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t fsRename(wasm_exec_env_t exec_env, const char *from, const char *to)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_rename: SD not mounted");
        return kWasmErrNotReady;
    }

    char from_host[256] = "";
    char to_host[256] = "";
    int32_t rc = make_host_path(from, from_host, sizeof(from_host));
    if (rc != kWasmOk) {
        return rc;
    }
    rc = make_host_path(to, to_host, sizeof(to_host));
    if (rc != kWasmOk) {
        return rc;
    }

    if (::rename(from_host, to_host) != 0) {
        const int e = errno;
        if (e == ENOENT) {
            wasm_api_set_last_error(kWasmErrNotFound, "fs_rename: not found");
            return kWasmErrNotFound;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_rename: rename failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t fsMkdir(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_mkdir: SD not mounted");
        return kWasmErrNotReady;
    }

    char host_path[256] = "";
    int32_t rc = make_host_path(path, host_path, sizeof(host_path));
    if (rc != kWasmOk) {
        return rc;
    }

    if (mkdir(host_path, 0777) != 0) {
        const int e = errno;
        if (e == EEXIST) {
            return kWasmOk;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_mkdir: mkdir failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t fsRmdir(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_rmdir: SD not mounted");
        return kWasmErrNotReady;
    }

    char host_path[256] = "";
    int32_t rc = make_host_path(path, host_path, sizeof(host_path));
    if (rc != kWasmOk) {
        return rc;
    }

    if (rmdir(host_path) != 0) {
        const int e = errno;
        if (e == ENOENT) {
            wasm_api_set_last_error(kWasmErrNotFound, "fs_rmdir: not found");
            return kWasmErrNotFound;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_rmdir: rmdir failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t fsOpendir(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    if (!sd_card_is_mounted()) {
        wasm_api_set_last_error(kWasmErrNotReady, "fs_opendir: SD not mounted");
        return kWasmErrNotReady;
    }

    char host_path[256] = "";
    int32_t rc = make_host_path(path, host_path, sizeof(host_path));
    if (rc != kWasmOk) {
        return rc;
    }

    DIR *d = opendir(host_path);
    if (!d) {
        const int e = errno;
        if (e == ENOENT) {
            wasm_api_set_last_error(kWasmErrNotFound, "fs_opendir: not found");
            return kWasmErrNotFound;
        }
        wasm_api_set_last_error(kWasmErrInternal, "fs_opendir: opendir failed");
        return kWasmErrInternal;
    }

    const int handle = alloc_dir_handle(d);
    if (handle == 0) {
        closedir(d);
        wasm_api_set_last_error(kWasmErrInternal, "fs_opendir: too many open dirs");
        return kWasmErrInternal;
    }

    return (int32_t)handle;
}

int32_t fsReaddir(wasm_exec_env_t exec_env, int32_t handle, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_readdir: out_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_readdir: out_len <= 0");
        return kWasmErrInvalidArgument;
    }

    DIR *d = get_dir(handle);
    if (!d) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_readdir: bad handle");
        return kWasmErrInvalidArgument;
    }

    errno = 0;
    struct dirent *de = readdir(d);
    if (!de) {
        if (errno != 0) {
            wasm_api_set_last_error(kWasmErrInternal, "fs_readdir: readdir failed");
            return kWasmErrInternal;
        }
        return 0; // end of directory
    }

    const size_t name_len = strlen(de->d_name);
    const size_t max_copy = (size_t)out_len - 1;
    const size_t to_copy = (name_len < max_copy) ? name_len : max_copy;
    memcpy(out_ptr, de->d_name, to_copy);
    out_ptr[to_copy] = '\0';
    return (int32_t)to_copy;
}

int32_t fsClosedir(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    return close_dir_handle(handle);
}

int32_t fsCardInfo(wasm_exec_env_t exec_env, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_card_info: out_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < (int32_t)kSdInfoSize) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fs_card_info: out_len too small (need 18)");
        return kWasmErrInvalidArgument;
    }

    memset(out_ptr, 0, kSdInfoSize);

    const sdmmc_card_t *card = sd_card_get_card();
    if (!sd_card_is_mounted() || !card) {
        out_ptr[0] = 0;
        return (int32_t)kSdInfoSize;
    }

    out_ptr[0] = 1;
    uint8_t type = 0;
    if (card->is_sdio) {
        type = 4;
    } else if (card->is_mmc) {
        type = 3;
    } else {
        type = (card->ocr & (1u << 30)) ? 2 : 1;
    }
    out_ptr[1] = type;

    const uint64_t capacity_bytes = (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    memcpy(out_ptr + 4, &capacity_bytes, sizeof(capacity_bytes));

    memset(out_ptr + 12, 0, 6);
    memcpy(out_ptr + 12, card->cid.name, 5);

    return (int32_t)kSdInfoSize;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_fs_native_symbols[] = {
    REG_NATIVE_FUNC(fsIsMounted, "()i"),
    REG_NATIVE_FUNC(fsMount, "()i"),
    REG_NATIVE_FUNC(fsUnmount, "()i"),

    REG_NATIVE_FUNC(fsOpen, "($i)i"),
    REG_NATIVE_FUNC(fsClose, "(i)i"),
    REG_NATIVE_FUNC(fsRead, "(i*i)i"),
    REG_NATIVE_FUNC(fsWrite, "(i*i)i"),
    REG_NATIVE_FUNC(fsSeek, "(iii)i"),

    REG_NATIVE_FUNC(fsStat, "($*i)i"),
    REG_NATIVE_FUNC(fsRemove, "($)i"),
    REG_NATIVE_FUNC(fsRename, "($$)i"),
    REG_NATIVE_FUNC(fsMkdir, "($)i"),
    REG_NATIVE_FUNC(fsRmdir, "($)i"),

    REG_NATIVE_FUNC(fsOpendir, "($)i"),
    REG_NATIVE_FUNC(fsReaddir, "(i*i)i"),
    REG_NATIVE_FUNC(fsClosedir, "(i)i"),
    REG_NATIVE_FUNC(fsCardInfo, "(*i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_fs(void)
{
    const uint32_t count = sizeof(g_fs_native_symbols) / sizeof(g_fs_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_fs", g_fs_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_fs natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_fs: wasm_runtime_register_natives failed");
    }
    return ok;
}
