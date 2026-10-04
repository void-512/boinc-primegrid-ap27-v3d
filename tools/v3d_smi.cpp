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
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

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

bool render(
    const StatsArray& previous,
    const StatsArray& current,
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

        while (!stop_requested) {

            const int rc =
                ::clock_nanosleep(
                    CLOCK_MONOTONIC,
                    TIMER_ABSTIME,
                    &next,
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

        if (!read_stats(
                current,
                read_buffer)) {

            std::perror(
                "read gpu_stats"
            );

            break;
        }

        std::size_t output_size = 0;


        if (!render(
                previous,
                current,
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

        add_ns(
            next,
            interval_ns
        );
    }

    leave_terminal_mode();
    ::close(stats_fd);

    return 0;
}
