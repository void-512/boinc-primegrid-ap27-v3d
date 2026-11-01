// Usage:
//   ./v3d-smi <frequency_hz>
//
// Examples:
//   ./v3d-smi 1
//   ./v3d-smi 10
//   ./v3d-smi 0.5

#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <v3d_drm.h>

namespace {

constexpr std::size_t QUEUE_COUNT = 5;

constexpr std::array<std::string_view, QUEUE_COUNT> QUEUE_NAMES{
    "bin",
    "render",
    "tfu",
    "csd",
    "cache_clean"
};

struct QueueStats {
    std::uint64_t timestamp;
    std::uint64_t runtime;
    bool valid;
};

using StatsArray = std::array<QueueStats, QUEUE_COUNT>;

constexpr std::size_t READ_BUFFER_SIZE = 4096;

constexpr std::size_t OUTPUT_BUFFER_SIZE = 512;


int stats_fd = -1;

volatile std::sig_atomic_t stop_requested = 0;

extern "C" void handle_signal(int) noexcept
{
    stop_requested = 1;
}

inline void add_ns(
    timespec& ts,
    std::int64_t ns) noexcept
{
    ts.tv_sec +=
        static_cast<time_t>(
            ns / 1'000'000'000LL
        );

    ts.tv_nsec +=
        static_cast<long>(
            ns % 1'000'000'000LL
        );

    if (ts.tv_nsec >= 1'000'000'000L) {
        ++ts.tv_sec;
        ts.tv_nsec -= 1'000'000'000L;
    }
}

bool find_gpu_stats(
    std::array<char, 4096>& path_buffer) noexcept
{
    namespace fs = std::filesystem;

    std::error_code ec;

    const fs::path root{
        "/sys/devices/platform"
    };

    fs::recursive_directory_iterator it{
        root,
        fs::directory_options::skip_permission_denied,
        ec
    };

    const fs::recursive_directory_iterator end{};

    if (ec)
        return false;

    for (; it != end; it.increment(ec)) {

        if (ec) {
            ec.clear();
            continue;
        }

        if (it->path().filename() != "gpu_stats")
            continue;

        std::error_code type_ec;

        if (!it->is_regular_file(type_ec))
            continue;

        const auto path =
            it->path().string();

        if (path.size() + 1 > path_buffer.size())
            return false;

        std::memcpy(
            path_buffer.data(),
            path.data(),
            path.size()
        );

        path_buffer[path.size()] = '\0';

        return true;
    }

    return false;
}

inline const char* skip_spaces(
    const char* p,
    const char* end) noexcept
{
    while (
        p < end &&
        (*p == ' ' || *p == '\t')
    ) {
        ++p;
    }

    return p;
}


inline const char* skip_token(
    const char* p,
    const char* end) noexcept
{
    while (
        p < end &&
        *p != ' ' &&
        *p != '\t' &&
        *p != '\n' &&
        *p != '\r'
    ) {
        ++p;
    }

    return p;
}


inline bool parse_u64(
    const char*& p,
    const char* end,
    std::uint64_t& value) noexcept
{
    p = skip_spaces(p, end);

    if (p >= end || *p < '0' || *p > '9')
        return false;

    std::uint64_t result = 0;

    do {
        result =
            result * 10 +
            static_cast<unsigned>(
                *p - '0'
            );

        ++p;

    } while (
        p < end &&
        *p >= '0' &&
        *p <= '9'
    );

    value = result;

    return true;
}


ssize_t read_text(int fd, std::array<char, READ_BUFFER_SIZE>& buffer) noexcept
{
    ssize_t size;
    do {
        size = ::pread(fd, buffer.data(), buffer.size(), 0);
    } while (size < 0 && errno == EINTR);
    return size;
}

std::string_view field(std::string_view text, std::string_view key) noexcept
{
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const auto line = text.substr(0, newline);
        if (line.size() > key.size() && line.substr(0, key.size()) == key &&
            line[key.size()] == ':') {
            auto value = line.substr(key.size() + 1);
            const auto start = value.find_first_not_of(" \t");
            if (start == std::string_view::npos)
                return {};
            value.remove_prefix(start);
            const auto end = value.find_last_not_of(" \t\r");
            return value.substr(0, end + 1);
        }
        if (newline == std::string_view::npos)
            break;
        text.remove_prefix(newline + 1);
    }
    return {};
}

bool parse_integer(std::string_view text, std::uint64_t& value) noexcept
{
    if (text.empty())
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_memory_bytes(std::string_view text, std::uint64_t& bytes) noexcept
{
    const auto separator = text.find_first_of(" \t");
    if (!parse_integer(text.substr(0, separator), bytes))
        return false;
    std::uint64_t scale = 1;
    if (separator != std::string_view::npos) {
        auto unit = text.substr(separator);
        const auto start = unit.find_first_not_of(" \t");
        if (start == std::string_view::npos)
            return false;
        unit.remove_prefix(start);
        if (unit == "KiB")
            scale = 1024;
        else if (unit == "MiB")
            scale = 1024 * 1024;
        else
            return false;
    }
    if (bytes > std::numeric_limits<std::uint64_t>::max() / scale)
        return false;
    bytes *= scale;
    return true;
}

bool active_memory_bytes(std::string_view text, std::uint64_t& bytes) noexcept
{
    const auto active = field(text, "drm-active-memory");
    if (!active.empty())
        return parse_memory_bytes(active, bytes);
    // DRM omits zero-valued active statistics, including on allocated idle BOs.
    if (!parse_memory_bytes(field(text, "drm-total-memory"), bytes))
        return false;
    bytes = 0;
    return true;
}

std::uint64_t monotonic_ns() noexcept
{
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
}

std::uint64_t to_ns(const timespec& time) noexcept
{
    return static_cast<std::uint64_t>(time.tv_sec) * 1'000'000'000 + time.tv_nsec;
}

int drm_ioctl(int fd, unsigned long request, void* data) noexcept
{
    int result;
    do {
        result = ::ioctl(fd, request, data);
    } while (result < 0 && errno == EINTR);
    return result;
}

struct BandwidthSample {
    std::array<std::uint64_t, 2> bytes{}; // Read, write; one AXI watch only.
    std::uint64_t timestamp = 0;
    bool valid = false;
};

class BandwidthMonitor {
    int fd_ = -1;
    std::uint32_t id_ = 0;
    bool created_ = false;
    bool enabled_ = false;

public:
    BandwidthMonitor() = default;
    BandwidthMonitor(const BandwidthMonitor&) = delete;
    BandwidthMonitor& operator=(const BandwidthMonitor&) = delete;

    ~BandwidthMonitor()
    {
        // Never clear somebody else's monitor if SET_GLOBAL failed.
        if (enabled_) {
            drm_v3d_perfmon_set_global clear{DRM_V3D_PERFMON_CLEAR_GLOBAL, id_};
            drm_ioctl(fd_, DRM_IOCTL_V3D_PERFMON_SET_GLOBAL, &clear);
        }
        if (created_) {
            drm_v3d_perfmon_destroy destroy{id_};
            drm_ioctl(fd_, DRM_IOCTL_V3D_PERFMON_DESTROY, &destroy);
        }
        if (fd_ >= 0)
            ::close(fd_);
    }

    bool initialize(const std::filesystem::path& device)
    {
        std::error_code ec;
        errno = ENODEV;
        for (const auto& entry : std::filesystem::directory_iterator(device / "drm", ec)) {
            const auto name = entry.path().filename().string();
            if (name.compare(0, 7, "renderD") == 0) {
                fd_ = ::open((std::filesystem::path("/dev/dri") / name).c_str(),
                             O_RDWR | O_CLOEXEC);
                break;
            }
        }
        if (fd_ < 0) {
            if (ec)
                errno = ec.value();
            return false;
        }

        drm_v3d_get_param parameter{};
        parameter.param = DRM_V3D_PARAM_MAX_PERF_COUNTERS;
        if (drm_ioctl(fd_, DRM_IOCTL_V3D_GET_PARAM, &parameter) < 0)
            return false;
        drm_v3d_perfmon_create create{};
        create.ncounters = 2;
        unsigned found = 0;
        // IDs differ between V3D generations: resolve names once at startup.
        for (unsigned i = 0; i < parameter.value && i <= 255; ++i) {
            drm_v3d_perfmon_get_counter counter{};
            counter.counter = static_cast<std::uint8_t>(i);
            if (drm_ioctl(fd_, DRM_IOCTL_V3D_PERFMON_GET_COUNTER, &counter) < 0)
                return false;
            const auto* name = reinterpret_cast<const char*>(counter.name);
            if (std::strcmp(name, "AXI-read-bytes-seen-watch-0") == 0) {
                create.counters[0] = counter.counter;
                found |= 1;
            } else if (std::strcmp(name, "AXI-write-bytes-seen-watch-0") == 0) {
                create.counters[1] = counter.counter;
                found |= 2;
            }
            if (found == 3)
                break;
        }
        if (found != 3) {
            errno = ENOTSUP;
            return false;
        }
        if (drm_ioctl(fd_, DRM_IOCTL_V3D_PERFMON_CREATE, &create) < 0)
            return false;
        id_ = create.id;
        created_ = true;
        drm_v3d_perfmon_set_global global{0, id_};
        enabled_ = drm_ioctl(fd_, DRM_IOCTL_V3D_PERFMON_SET_GLOBAL, &global) == 0;
        return enabled_;
    }

    bool enabled() const noexcept { return enabled_; }

    BandwidthSample sample() noexcept
    {
        BandwidthSample result;
        if (enabled_) {
            drm_v3d_perfmon_get_values request{};
            request.id = id_;
            request.values_ptr = reinterpret_cast<std::uintptr_t>(result.bytes.data());
            result.valid = drm_ioctl(fd_, DRM_IOCTL_V3D_PERFMON_GET_VALUES, &request) == 0;
        }
        result.timestamp = monotonic_ns();
        return result;
    }
};

struct ActiveMemorySample {
    std::uint64_t bytes = 0;
    bool valid = false;
};

class ActiveMemoryMonitor {
    struct Client {
        std::uint64_t id;
        int fd;
        bool seen;
    };
    std::vector<Client> clients_;
    std::string device_name_;
    std::uint64_t next_discovery_ = 0;
    bool complete_ = false;

    void close_clients() noexcept
    {
        for (const auto& client : clients_)
            if (client.fd >= 0)
                ::close(client.fd);
        clients_.clear();
    }

    bool open_client(std::uint64_t pid, std::uint64_t id)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::directory_iterator it(fs::path("/proc") / std::to_string(pid) / "fdinfo", ec);
        const fs::directory_iterator end;
        std::array<char, READ_BUFFER_SIZE> buffer{};
        for (; !ec && it != end; it.increment(ec)) {
            const int fd = ::open(it->path().c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0)
                continue;
            const ssize_t size = read_text(fd, buffer);
            if (size > 0 && static_cast<std::size_t>(size) < buffer.size()) {
                const std::string_view text(buffer.data(), static_cast<std::size_t>(size));
                std::uint64_t client_id;
                if (field(text, "drm-driver") == "v3d" &&
                    parse_integer(field(text, "drm-client-id"), client_id) && client_id == id) {
                    clients_.push_back({id, fd, true});
                    return true;
                }
            }
            ::close(fd);
        }
        return false;
    }

    void discover()
    {
        namespace fs = std::filesystem;
        for (auto& client : clients_)
            client.seen = false;
        std::error_code ec;
        fs::directory_iterator it("/sys/kernel/debug/dri", ec);
        complete_ = !ec;
        const fs::directory_iterator end;
        std::array<char, READ_BUFFER_SIZE> buffer{};
        // Only inspect DRM client processes, never walk every process's fds.
        for (; !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (name.compare(0, 7, "client-") != 0)
                continue;
            std::error_code link_ec;
            const auto device = fs::read_symlink(it->path() / "device", link_ec);
            if (link_ec) {
                complete_ = false;
                continue;
            }
            if (device.filename() != device_name_)
                continue;
            std::uint64_t id;
            if (!parse_integer(std::string_view(name).substr(7), id)) {
                complete_ = false;
                continue;
            }
            bool cached = false;
            for (auto& client : clients_) {
                if (client.id == id && client.fd >= 0) {
                    client.seen = true;
                    cached = true;
                    break;
                }
            }
            if (cached)
                continue;
            const int fd = ::open((it->path() / "proc_info").c_str(), O_RDONLY | O_CLOEXEC);
            const ssize_t size = fd >= 0 ? read_text(fd, buffer) : -1;
            if (fd >= 0)
                ::close(fd);
            std::uint64_t pid;
            if (size <= 0 || !parse_integer(field(
                    std::string_view(buffer.data(), static_cast<std::size_t>(size)), "pid"), pid)) {
                complete_ = false;
                continue;
            }
            if (pid == static_cast<std::uint64_t>(::getpid()))
                continue; // Our counter-only DRM client has no buffers.
            if (!open_client(pid, id))
                complete_ = false;
        }
        if (ec)
            complete_ = false;
        for (auto client = clients_.begin(); client != clients_.end();) {
            if (!client->seen) {
                if (client->fd >= 0)
                    ::close(client->fd);
                client = clients_.erase(client);
            } else {
                ++client;
            }
        }
    }

public:
    explicit ActiveMemoryMonitor(const std::filesystem::path& device)
        : device_name_(device.filename().string()) {}
    ActiveMemoryMonitor(const ActiveMemoryMonitor&) = delete;
    ActiveMemoryMonitor& operator=(const ActiveMemoryMonitor&) = delete;
    ~ActiveMemoryMonitor() { close_clients(); }

    ActiveMemorySample sample(std::uint64_t now)
    {
        if (now >= next_discovery_) {
            discover();
            next_discovery_ = now + 5'000'000'000;
        }
        ActiveMemorySample result{0, complete_};
        std::array<char, READ_BUFFER_SIZE> buffer{};
        for (auto& client : clients_) {
            const ssize_t size = read_text(client.fd, buffer);
            if (size <= 0 || static_cast<std::size_t>(size) == buffer.size()) {
                result.valid = false;
                next_discovery_ = 0;
                ::close(client.fd);
                client.fd = -1;
                continue;
            }
            const std::string_view text(buffer.data(), static_cast<std::size_t>(size));
            std::uint64_t id, bytes;
            if (!parse_integer(field(text, "drm-client-id"), id) || id != client.id) {
                result.valid = false;
                next_discovery_ = 0;
                ::close(client.fd);
                client.fd = -1;
            } else if (active_memory_bytes(text, bytes)) {
                result.bytes += bytes;
            } else {
                result.valid = false;
            }
        }
        return result;
    }
};

inline int queue_index(
    const char* begin,
    std::size_t length) noexcept
{

    switch (length) {

        case 3:
            if (
                begin[0] == 'b' &&
                begin[1] == 'i' &&
                begin[2] == 'n'
            )
                return 0;

            if (
                begin[0] == 't' &&
                begin[1] == 'f' &&
                begin[2] == 'u'
            )
                return 2;

            if (
                begin[0] == 'c' &&
                begin[1] == 's' &&
                begin[2] == 'd'
            )
                return 3;

            break;


        case 6:
            if (
                std::memcmp(
                    begin,
                    "render",
                    6
                ) == 0
            )
                return 1;

            break;


        case 11:
            if (
                std::memcmp(
                    begin,
                    "cache_clean",
                    11
                ) == 0
            )
                return 4;

            break;
    }

    return -1;
}

bool parse_stats(
    const char* data,
    std::size_t size,
    StatsArray& stats) noexcept
{
    for (auto& s : stats)
        s.valid = false;

    const char* p = data;
    const char* const end = data + size;

    // Skip header line.
    while (p < end && *p != '\n')
        ++p;

    if (p < end)
        ++p;


    while (p < end) {

        // Skip blank lines.
        while (
            p < end &&
            (*p == '\n' || *p == '\r')
        ) {
            ++p;
        }

        if (p >= end)
            break;

        const char* const queue_begin = p;

        p = skip_token(p, end);

        const std::size_t queue_length =
            static_cast<std::size_t>(
                p - queue_begin
            );

        const int index =
            queue_index(
                queue_begin,
                queue_length
            );

        std::uint64_t timestamp = 0;

        if (!parse_u64(
                p,
                end,
                timestamp)) {

            while (p < end && *p != '\n')
                ++p;

            continue;
        }

        p = skip_spaces(p, end);
        p = skip_token(p, end);

        std::uint64_t runtime = 0;

        if (!parse_u64(
                p,
                end,
                runtime)) {

            while (p < end && *p != '\n')
                ++p;

            continue;
        }


        if (index >= 0) {

            auto& s =
                stats[
                    static_cast<std::size_t>(
                        index
                    )
                ];

            s.timestamp = timestamp;
            s.runtime = runtime;
            s.valid = true;
        }


        // Skip rest of line.
        while (p < end && *p != '\n')
            ++p;

        if (p < end)
            ++p;
    }

    return true;
}

bool read_stats(
    StatsArray& stats,
    std::array<char, READ_BUFFER_SIZE>& buffer) noexcept
{
    /*
     * pread() reads from offset 0 without:
     *
     *   lseek(fd, 0, SEEK_SET);
     *   read(...);
     *
     * so one syscall is sufficient.
     */

    ssize_t n;

    do {

        n = ::pread(
            stats_fd,
            buffer.data(),
            buffer.size(),
            0
        );

    } while (
        n < 0 &&
        errno == EINTR
    );


    if (n <= 0)
        return false;


    return parse_stats(
        buffer.data(),
        static_cast<std::size_t>(n),
        stats
    );
}

inline bool append_chars(
    char*& out,
    const char* end,
    const char* data,
    std::size_t length) noexcept
{
    if (
        static_cast<std::size_t>(
            end - out
        ) < length
    ) {
        return false;
    }

    std::memcpy(
        out,
        data,
        length
    );

    out += length;

    return true;
}


template<std::size_t N>
inline bool append_literal(
    char*& out,
    const char* end,
    const char (&text)[N]) noexcept
{
    return append_chars(
        out,
        end,
        text,
        N - 1
    );
}


// Format queue name exactly as:
//
// %-14s
//
inline bool append_queue_name(
    char*& out,
    const char* end,
    std::string_view name) noexcept
{
    constexpr std::size_t WIDTH = 14;

    if (
        static_cast<std::size_t>(
            end - out
        ) < WIDTH
    ) {
        return false;
    }

    std::memcpy(
        out,
        name.data(),
        name.size()
    );

    out += name.size();

    const std::size_t padding =
        WIDTH - name.size();

    std::memset(
        out,
        ' ',
        padding
    );

    out += padding;

    return true;
}

inline bool append_percentage(
    char*& out,
    const char* end,
    std::uint64_t runtime,
    std::uint64_t elapsed) noexcept
{
    if (elapsed == 0)
        return false;

    /*
     * Original Bash output:
     *
     *     printf "%6.2f%%"
     *
     * We calculate hundredths of one percent using integer arithmetic:
     *
     *      runtime / elapsed * 100
     *
     * represented as:
     *
     *      runtime * 10000 / elapsed
     *
     * __uint128_t prevents overflow of the intermediate multiplication.
     *
     * Add elapsed / 2 for nearest-integer rounding.
     */

    const __uint128_t numerator =
        static_cast<__uint128_t>(runtime) *
        10'000u;

    std::uint64_t hundredths =
        static_cast<std::uint64_t>(
            (numerator + elapsed / 2) /
            elapsed
        );


    // The scheduler should normally remain <=100%, but don't impose a
    // synthetic limit in case the kernel reports otherwise.

    const std::uint64_t whole =
        hundredths / 100;

    const unsigned fraction =
        static_cast<unsigned>(
            hundredths % 100
        );


    // Convert integer part into a tiny temporary buffer.
    char number[32];

    auto result =
        std::to_chars(
            number,
            number + sizeof(number),
            whole
        );

    if (result.ec != std::errc{})
        return false;


    const std::size_t whole_length =
        static_cast<std::size_t>(
            result.ptr - number
        );


    /*
     * "%6.2f" means:
     *
     * whole digits + "." + 2 fraction digits
     *
     * should occupy at least six characters.
     */

    const std::size_t numeric_length =
        whole_length + 3;

    const std::size_t padding =
        numeric_length < 6
            ? 6 - numeric_length
            : 0;


    if (
        static_cast<std::size_t>(
            end - out
        ) <
        padding + numeric_length + 2
    ) {
        return false;
    }


    for (std::size_t i = 0; i < padding; ++i)
        *out++ = ' ';


    std::memcpy(
        out,
        number,
        whole_length
    );

    out += whole_length;


    *out++ = '.';

    *out++ =
        static_cast<char>(
            '0' + fraction / 10
        );

    *out++ =
        static_cast<char>(
            '0' + fraction % 10
        );

    *out++ = '%';
    *out++ = '\n';

    return true;
}

bool append_metric(char*& out, const char* end, std::string_view label,
                   double value, bool valid, std::string_view unit) noexcept
{
    if (!append_queue_name(out, end, label))
        return false;
    if (valid) {
        char number[64];
        const auto result = std::to_chars(number, number + sizeof(number), value,
                                          std::chars_format::fixed, 2);
        if (result.ec != std::errc{} ||
            !append_chars(out, end, number, static_cast<std::size_t>(result.ptr - number)))
            return false;
    } else if (!append_literal(out, end, "N/A")) {
        return false;
    }
    return append_literal(out, end, " ") &&
           append_chars(out, end, unit.data(), unit.size()) &&
           append_literal(out, end, "\n");
}

bool render(
    const StatsArray& previous,
    const StatsArray& current,
    const BandwidthSample& previous_bandwidth,
    const BandwidthSample& bandwidth,
    const ActiveMemorySample& memory,
    std::array<char, OUTPUT_BUFFER_SIZE>& buffer,
    std::size_t& output_size) noexcept
{
    char* out = buffer.data();

    char* const end =
        buffer.data() + buffer.size();

    /*
     * ESC[H  -> cursor to top-left
     * ESC[2J -> clear entire visible screen
     *
     * This behaves like `clear` for the visible terminal,
     * without spawning an external process.
     */
    if (!append_literal(
        out,
        end,
        "\x1b[H"
        "\x1b[2J"
        "V3D queue utilisation\n\n")) {
            return false;
    }

    for (std::size_t i = 0; i < QUEUE_COUNT; ++i) {

        const auto& before = previous[i];
        const auto& after  = current[i];

        if (!before.valid || !after.valid)
            continue;

        if (after.timestamp <= before.timestamp)
            continue;

        if (after.runtime < before.runtime)
            continue;

        const std::uint64_t elapsed =
            after.timestamp - before.timestamp;

        const std::uint64_t runtime =
            after.runtime - before.runtime;

        if (!append_queue_name(
                out,
                end,
                QUEUE_NAMES[i])) {
            return false;
        }

        if (!append_percentage(
                out,
                end,
                runtime,
                elapsed)) {
            return false;
        }
    }

    const bool bandwidth_valid = previous_bandwidth.valid && bandwidth.valid &&
        bandwidth.timestamp > previous_bandwidth.timestamp &&
        bandwidth.bytes[0] >= previous_bandwidth.bytes[0] &&
        bandwidth.bytes[1] >= previous_bandwidth.bytes[1];
    double read_mbps = 0, write_mbps = 0;
    if (bandwidth_valid) {
        const double elapsed = static_cast<double>(bandwidth.timestamp - previous_bandwidth.timestamp);
        read_mbps = static_cast<double>(bandwidth.bytes[0] - previous_bandwidth.bytes[0]) * 1000 / elapsed;
        write_mbps = static_cast<double>(bandwidth.bytes[1] - previous_bandwidth.bytes[1]) * 1000 / elapsed;
    }
    if (!append_literal(out, end, "\n") ||
        !append_metric(out, end, "GPU read", read_mbps, bandwidth_valid, "MB/s") ||
        !append_metric(out, end, "GPU write", write_mbps, bandwidth_valid, "MB/s") ||
        !append_metric(out, end, "Active VRAM", static_cast<double>(memory.bytes) / 1'000'000,
                       memory.valid, "MB"))
        return false;

    output_size =
        static_cast<std::size_t>(
            out - buffer.data()
        );

    return true;
}

bool write_all(
    int fd,
    const char* data,
    std::size_t size) noexcept
{
    while (size != 0) {

        const ssize_t n =
            ::write(
                fd,
                data,
                size
            );


        if (n > 0) {

            data += n;

            size -=
                static_cast<std::size_t>(n);

            continue;
        }


        if (
            n < 0 &&
            errno == EINTR
        ) {
            continue;
        }


        return false;
    }

    return true;
}

bool enter_terminal_mode() noexcept
{
    if (!::isatty(STDOUT_FILENO))
        return true;

    constexpr char seq[] =
        "\x1b[?1049h"  // enter alternate screen
        "\x1b[?25l"    // hide cursor
        "\x1b[H"       // cursor home
        "\x1b[2J";     // clear alternate screen

    return write_all(
        STDOUT_FILENO,
        seq,
        sizeof(seq) - 1
    );
}

void leave_terminal_mode() noexcept
{
    if (!::isatty(STDOUT_FILENO))
        return;

    constexpr char seq[] =
        "\x1b[?25h"    // show cursor
        "\x1b[?1049l"; // restore original screen

    write_all(
        STDOUT_FILENO,
        seq,
        sizeof(seq) - 1
    );
}

bool parse_frequency(
    const char* text,
    double& hz) noexcept
{
    const char* const begin =
        text;

    const char* const end =
        text +
        std::strlen(text);


    const auto result =
        std::from_chars(
            begin,
            end,
            hz
        );


    return
        result.ec == std::errc{} &&
        result.ptr == end &&
        std::isfinite(hz) &&
        hz > 0.0;
}

void print_usage(
    const char* executable) noexcept
{
    std::fprintf(
        stderr,
        "Usage: %s <frequency_hz>\n"
        "\n"
        "Examples:\n"
        "  %s 1\n"
        "  %s 10\n"
        "  %s 0.5\n",
        executable,
        executable,
        executable,
        executable
    );
}

}

int main(
    int argc,
    char* argv[])
{

    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }


    double frequency_hz = 0.0;

    if (!parse_frequency(
            argv[1],
            frequency_hz)) {

        std::fprintf(
            stderr,
            "Invalid frequency: %s\n",
            argv[1]
        );

        return 1;
    }


    const double interval_double =
        1'000'000'000.0 /
        frequency_hz;


    if (
        interval_double < 1.0 ||
        interval_double >
            static_cast<double>(
                std::numeric_limits<
                    std::int64_t
                >::max()
            )
    ) {

        std::fprintf(
            stderr,
            "Frequency is outside the supported range.\n"
        );

        return 1;
    }


    const auto interval_ns =
        static_cast<std::int64_t>(
            std::llround(
                interval_double
            )
        );

    struct sigaction sa{};

    sa.sa_handler = handle_signal;

    ::sigemptyset(
        &sa.sa_mask
    );


    if (
        ::sigaction(
            SIGINT,
            &sa,
            nullptr
        ) != 0
    ) {
        std::perror("sigaction");
        return 1;
    }


    if (
        ::sigaction(
            SIGTERM,
            &sa,
            nullptr
        ) != 0
    ) {
        std::perror("sigaction");
        return 1;
    }

    // A closed pipe must unwind the monitors and release the global counters.
    if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        std::perror("signal SIGPIPE");
        return 1;
    }

    std::array<char, 4096> stats_path{};


    if (!find_gpu_stats(stats_path)) {

        std::fprintf(
            stderr,
            "V3D gpu_stats is not available in this kernel.\n"
        );

        return 1;
    }

    stats_fd =
        ::open(
            stats_path.data(),
            O_RDONLY |
            O_CLOEXEC
        );


    if (stats_fd < 0) {

        std::perror(
            "open gpu_stats"
        );

        return 1;
    }

    StatsArray previous{};
    StatsArray current{};

    const auto device = std::filesystem::path(stats_path.data()).parent_path();
    BandwidthMonitor bandwidth_monitor;
    if (!bandwidth_monitor.initialize(device))
        std::fprintf(stderr, "Bandwidth unavailable: %s\n", std::strerror(errno));
    auto previous_bandwidth = bandwidth_monitor.sample();
    auto bandwidth = previous_bandwidth;
    ActiveMemoryMonitor memory_monitor(device);
    auto memory = memory_monitor.sample(bandwidth.timestamp);
    if (!memory.valid)
        std::fprintf(stderr, "Active VRAM unavailable; access to DRM debugfs and GPU process fdinfo is required (try sudo).\n");

    alignas(64)
    std::array<
        char,
        READ_BUFFER_SIZE
    > read_buffer{};

    alignas(64)
    std::array<
        char,
        OUTPUT_BUFFER_SIZE
    > output_buffer{};

    if (!read_stats(
            current,
            read_buffer)) {

        std::perror(
            "read gpu_stats"
        );

        ::close(stats_fd);

        return 1;
    }


    previous = current;

    timespec next{};


    if (
        ::clock_gettime(
            CLOCK_MONOTONIC,
            &next
        ) != 0
    ) {

        std::perror(
            "clock_gettime"
        );

        ::close(stats_fd);

        return 1;
    }


    // First output occurs after one complete measurement interval,
    // matching the behaviour of the Bash script.
    add_ns(
        next,
        interval_ns
    );

    if (!enter_terminal_mode()) {
        std::perror("enter terminal mode");
        ::close(stats_fd);
        return 1;
    }

    while (!stop_requested) {

        timespec wakeup = next;
        if (bandwidth_monitor.enabled()) {
            // AXI byte counters are 32-bit. Capture at least every 100 ms so
            // they cannot wrap at Pi 5 memory speeds, even with slow refresh.
            const auto capture_deadline = bandwidth.timestamp + 100'000'000;
            if (capture_deadline < to_ns(wakeup)) {
                wakeup.tv_sec = static_cast<time_t>(capture_deadline / 1'000'000'000);
                wakeup.tv_nsec = static_cast<long>(capture_deadline % 1'000'000'000);
            }
        }

        while (!stop_requested) {

            const int rc =
                ::clock_nanosleep(
                    CLOCK_MONOTONIC,
                    TIMER_ABSTIME,
                    &wakeup,
                    nullptr
                );


            if (rc == 0)
                break;


            if (rc == EINTR)
                continue;


            errno = rc;

            std::perror(
                "clock_nanosleep"
            );

            stop_requested = 1;

            break;
        }


        if (stop_requested)
            break;

        bandwidth = bandwidth_monitor.sample();
        if (bandwidth.timestamp < to_ns(next))
            continue;

        if (!read_stats(
                current,
                read_buffer)) {

            std::perror(
                "read gpu_stats"
            );

            break;
        }

        memory = memory_monitor.sample(bandwidth.timestamp);
        std::size_t output_size = 0;


        if (!render(
                previous,
                current,
                previous_bandwidth,
                bandwidth,
                memory,
                output_buffer,
                output_size)) {

            std::fprintf(
                stderr,
                "Output buffer overflow.\n"
            );

            break;
        }

        if (!write_all(
                STDOUT_FILENO,
                output_buffer.data(),
                output_size)) {

            std::perror(
                "write stdout"
            );

            break;
        }


        // Fixed-size copy: five QueueStats.
        previous = current;
        previous_bandwidth = bandwidth;

        add_ns(
            next,
            interval_ns
        );
    }

    leave_terminal_mode();
    ::close(stats_fd);

    return 0;
}
