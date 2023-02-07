// vi: textwidth=100
#pragma once

#include "sane_wrapper_utils.h"

#ifdef SANE_PP_STUB
#include "sane_wrapper_stub.h"
#endif

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <list>
#include <set>
#include <bitset>
#include <ranges>
#include <variant>
#include <span>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <sane/sane.h>

namespace vg_sane {

class device;

enum class LogLevel : char { Debug, Info, Warn };

using devices_t = std::ranges::subrange<const ::SANE_Device**>;
using logger_sink_t = std::function<void(LogLevel, std::string_view)>;

/**
 * A holder of various Sane types. Indexed as:
 *   [0] (std::monostate) - for SANE_TYPE_BUTTON, SANE_TYPE_GROUP
 *   [1] (::SANE_Word&) - for SANE_TYPE_BOOL
 *   [2] (std::span<::SANE_Word>) - for SANE_TYPE_INT, SANE_TYPE_FIXED (they could be arrays)
 *   [3] (::SANE_String) - for SANE_TYPE_STRING
 * Underlying types are exposed here via C++ wrapper public interface for increasing efficiency
 * because the C library can slighly modify an option value even while a caller wants to set it for
 * a device. But it's still a design question: whether it's better to fully isolate underlying
 * library and the way data types are represented?
 */
using opt_value_t = std::variant<
    std::monostate, std::reference_wrapper<::SANE_Word>, std::span<::SANE_Word>, ::SANE_String>;

inline const char* to_string(LogLevel sev) {
    switch (sev) {
    case LogLevel::Debug: return "[Debug]";
    case LogLevel::Info: return "[Info]";
    case LogLevel::Warn: return "[Warn]";
    default: return "[???]";
    }
}

/**
 * Represents Sane library wrapper which is a singleton for the whole process. So the object can't
 * be created via constructor but instead is returned as a smart pointer by calling instance() method.
 * Only one object exists at any time. Any device object got from it internally saves this singleton
 * from deletion even if no other pointers to the library exist.
 */
class lib final {
public:
    struct lib_internal;

    using ptr_t = std::shared_ptr<lib>;
    using wptr_t = std::weak_ptr<lib>;

    static ptr_t instance();

    /**
     * @returns forward-iterable range of Sane device infos. No caching, each call to this method
     *    makes a call to underlying C library.
     */
    devices_t get_device_infos() const;

    /**
     * Open a scanner device specified by name. Only one scanner with specified name can exist in
     * the process - this wrapper library checks it. The call can fail even if such a name was
     * observed by enumerating entities from get_device_infos() call. A real scanner could be
     * unplugged between two calls.
     */
    device open_device(const char* name);

    void set_logger_sink(logger_sink_t cb = {}) {
        m_logger_sink = cb;
    }

    ~lib();

private:
    static wptr_t m_inst_wptr;

    std::unique_ptr<lib_internal> m_internal_iface;
    ::SANE_Int m_sane_ver;
    std::set<std::string> m_opened_device_names;
    logger_sink_t m_logger_sink;

    lib();

    lib(const lib&) = delete;
    lib& operator=(const lib&) = delete;
};

/**
 * Represents particular scanner. The object is not copyable because the scanner can't be copied in
 * real world.
 */
class device final {
    friend device lib::open_device(const char*);

public:
    enum class set_opt_result_flags : char {
        value_inexact = 0,
        reload_opts = 1,
        reload_params = 2,
        last
    };

    using set_opt_result_t = std::bitset<static_cast<std::size_t>(set_opt_result_flags::last)>;

    struct option_iterator final {
        using value_type = std::pair<int, const ::SANE_Option_Descriptor*>;

        option_iterator() {} // option_iterator() = default; - should be the same really, but
                             // looks like gcc-11.3 is buggy here

        value_type operator*() const {
            return {m_pos, m_parent_device->get_option_info(m_pos)};
        }
        value_type operator[](int n) const {
            return {m_pos + n, m_parent_device->get_option_info(m_pos + n)};
        }
        option_iterator& operator++() { ++m_pos; return *this; }
        option_iterator operator++(int) { auto t = *this; ++m_pos; return t; }
        option_iterator& operator--() { --m_pos; return *this; }
        option_iterator operator--(int) { auto t = *this; --m_pos; return t; }
        option_iterator& operator+=(int n) { m_pos += n; return *this; }
        option_iterator& operator-=(int n) { m_pos -= n; return *this; }
        int operator-(const option_iterator& r) const { return m_pos - r.m_pos; }
        auto operator<=>(const option_iterator&) const = default;

        friend option_iterator operator+(const option_iterator& l, int r) {
            return {l.m_parent_device, l.m_pos + r};
        }
        friend option_iterator operator-(const option_iterator& l, int r) {
            return {l.m_parent_device, l.m_pos - r};
        }
        friend option_iterator operator+(int l, const option_iterator& r) {
            return {r.m_parent_device, l + r.m_pos};
        }
        friend option_iterator operator-(int l, const option_iterator& r) {
            return {r.m_parent_device, l - r.m_pos};
        }

    private:
        friend device;

        const device* m_parent_device = {};
        int m_pos = 1;

        option_iterator(const device* parent_device, int pos)
            : m_parent_device{parent_device}
            , m_pos{pos} {
        }
    };

    device() = default;
    device(device&& r);
    ~device();

    device& operator=(device&& r) {
        auto t = std::move(r);
        swap(t);
        return *this;
    }

    void swap(device& r) {
        using std::swap;
        swap(m_handle, r.m_handle);
        swap(m_name, r.m_name);
        swap(m_lib_internal, r.m_lib_internal);
        swap(m_deletion_cb, r.m_deletion_cb);
    }

    const std::string& name() const { return m_name; }

    /**
     * @return a range with option descriptors for every available option of a scanner (even
     *   inactive or un-editable)
     */
    std::ranges::subrange<option_iterator> get_option_infos() const;
    opt_value_t get_option(int pos) const;
    set_opt_result_t set_option(int pos, opt_value_t val);

    /**
     * Start asynchronous operation for fetching a frame of visual data from a scanner (the process
     * is called "image acquisition" in SANE docs). At start of scanning the device determines
     * actual image properties which can be observed by get_scanning_parameters() getter after the
     * first state change. All further blocks of data are returned by repeated call of get_scanning_data()
     * method. It returns zero sized buffer at normal end or if the operation has been cancelled.
     * In all other circumstances any getter can throw an exception carrying information about
     * latest error in the scanning process. The process is considered finished after this.
     */
    void start_scanning(std::function<void()> cb = {});

    /**
     * @returns scanning parameters of current image or nullptr of called too early. In case of
     *    synchronous mode (cb wasn't provided for start op) - waits until parameters got from the
     *    device
     */
    const ::SANE_Parameters* get_scanning_parameters();

    /**
     * The method can be called only after get_scanning_parameters() returned non-null pointer in
     * asynchronous mode.
     *
     * @returns next block of image data or empty buffer in case of end-of-stream (which can be
     *    caused by requested cancelling also)
     */
    std::vector<unsigned char> get_scanning_data();

    /**
     * Cancels currently running scan operation asynchronously. The operation can be considered
     * cancelled only when get_scanning_data() returns empty buffer.
     */
    void cancel_scanning(bool fast = false);

private:
#ifdef SANE_PP_STUB
    using handle_t = std::vector<std::shared_ptr<details::stub_option>>;
#else
    using handle_t = ::SANE_Handle;
#endif

    using deletion_cb_t = std::function<void(const std::string&)>;

    enum class ScanningState : char {
        Idle, Initializing, Starting, Scanning
    };

#ifdef SANE_PP_STUB
    mutable handle_t m_handle;
    std::size_t m_sample_image_offset;
#else
    handle_t m_handle = {};
    // SANE library requires a storage to place an option data into
    mutable std::vector<char> m_option_data_buffer;
#endif

    std::string m_name;
    lib::lib_internal* m_lib_internal;
    deletion_cb_t m_deletion_cb; // locks the library singleton inside a lambda, stored here

    std::mutex m_scanning_state_mutex;
    std::function<void()> m_scanning_state_notifier;
    bool m_use_internal_waiter;
    bool m_use_asynchronous_mode;
    std::condition_variable m_internal_state_waiting;
    ScanningState m_scanning_state = ScanningState::Idle;
    std::exception_ptr m_last_scanning_error;
    ::SANE_Parameters m_scanning_params;
    std::list<std::vector<unsigned char>> m_chunks;
    int m_waiter_pipes[2];

    // Should be the last member here to join the thread in exceptional cases before other members
    // go away
    std::jthread m_scanning_thread;

    device(handle_t dev_handle, std::string name, lib::lib_internal* lib_int, deletion_cb_t deletion_cb);

    const ::SANE_Option_Descriptor* get_option_info(int pos) const;
    void do_scanning(std::stop_token);
    void check_for_scanning_error(std::unique_lock<std::mutex>&& lock);
};

inline void swap(device& l, device& r) { l.swap(r); }

using device_options_t = std::ranges::subrange<device::option_iterator>;

static_assert(std::bidirectional_iterator<device::option_iterator>);
static_assert(std::ranges::sized_range<decltype(std::declval<device>().get_option_infos())>);

} // ns vg_sane
