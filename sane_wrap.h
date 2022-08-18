#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <iterator>
#include <vector>
#include <compare>
#include <ranges>
#include <functional>

#include <sane/sane.h>

constexpr bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}

namespace sg_sane {

class error : public std::runtime_error {
public:
    using runtime_error::runtime_error;
};

class error_with_code : public error {
    static std::string get_msg(std::string msg_prefix, ::SANE_Status status) {
        auto msg = ::sane_strstatus(status);
        if (msg_prefix.empty())
            return msg;
        else {
            msg_prefix.append(": ");
            msg_prefix.append(msg);
            return msg_prefix;
        }
    }

public:
    explicit error_with_code(std::string msg_prefix, ::SANE_Status status)
        : error(get_msg(std::move(msg_prefix), status))
    {
    }
};

namespace details {

template <class Msg, class ... Parms, class ... Args>
void checked_call(Msg&& msg, ::SANE_Status (*f)(Parms ...), Args&& ... args) {
    if (auto status = (*f)(std::forward<Args>(args) ...); status != SANE_STATUS_GOOD) {
        if constexpr (std::is_convertible_v<std::decay_t<Msg>, std::string>) {
            throw error_with_code(std::forward<Msg>(msg), status);
        } else if constexpr (std::is_invocable_v<Msg>) {
            throw error_with_code(std::invoke(std::forward<Msg>(msg)), status);
        } else
            static_assert(!sizeof(Msg));
    }
}

} // ns details

class device;

/**
 * Represents Sane library which is a singleton for the whole process. So the object can't be
 * created via constructor but instead is returned by calling instance() method. Only one object
 * exists at any time.
 */
class lib final {
public:
    using ptr_t = std::shared_ptr<lib>;
    using wptr_t = std::weak_ptr<lib>;

    struct device_info_iterator;

    // TODO: will do something with dangling pointers? Though it should be
    // short living object. Whether it adds any additional safety or it's
    // simpler to return just raw ::SANE_Device pointers on iterating?
    struct device_info final {
        const char* name() const { return m_impl->name; }
        const char* vendor() const { return m_impl->vendor; }
        const char* model() const { return m_impl->model; }
        const char* type() const { return m_impl->type; }
    private:
        friend device_info_iterator;
        const ::SANE_Device* m_impl;

        device_info() = default;
        device_info(const ::SANE_Device* impl) : m_impl(impl)
        {
        }
    };

    /**
     * The iterator represents a view range of device infos observable by a user of the library.
     * It's a short-living object invalidated when the library is closed and also invalidated if
     * device infos has been reloaded.
     */
    struct device_info_iterator final {
        using difference_type = std::iterator_traits<SANE_Device**>::difference_type;
        using value_type = device_info;
        using reference = device_info;
        using pointer = const device_info*;
        using iterator_category = std::bidirectional_iterator_tag;

        device_info_iterator() = default;

        reference operator*() const { return *m_devices; }
        pointer operator->() const {
            static device_info t;
            t.m_impl = *m_devices;
            return &t;
        }
        auto& operator++() { ++m_devices; return *this; }
        auto operator++(int) { auto r = *this; ++m_devices; return r; }
        auto& operator--() { --m_devices; return *this; }
        auto operator--(int) { auto r = *this; --m_devices; return r; }

        bool operator==(const device_info_iterator& r) const {
            return m_devices == r.m_devices;
        }
        friend bool operator==(const device_info_iterator& l, std::default_sentinel_t) {
            return ! *l.m_devices;
        }

    private:
        friend lib;
        static inline const ::SANE_Device* m_devices_end = nullptr;
        const ::SANE_Device** m_devices = &m_devices_end;

        device_info_iterator(const ::SANE_Device** devices) : m_devices(devices)
        {
        }
    };

    auto get_device_infos(bool reload = false) {
        if (! m_devices || reload) {
            m_devices = {};
            details::checked_call("unable to get list of devices", ::sane_get_devices, &m_devices, SANE_TRUE);
        }

        return std::ranges::subrange(
            device_info_iterator{m_devices}, std::default_sentinel);
    }

    auto get_device_infos_raw(bool reload = false) {
        if (! m_devices || reload) {
            m_devices = {};
            details::checked_call("unable to get list of devices", ::sane_get_devices, &m_devices, SANE_TRUE);
        }

        return std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>(
            &m_devices[0], std::default_sentinel);
    }

    device open_device(const char* name);

    static ptr_t instance() {
        if (auto p = m_inst_wptr.lock())
            return p;
        auto p = ptr_t{new lib};
        m_inst_wptr = p;
        return p;
    }

    ~lib() {
        ::sane_exit();
    }

private:
    static wptr_t m_inst_wptr;

    ::SANE_Int m_sane_ver;
    const ::SANE_Device **m_devices = {};

    lib() { details::checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr); }

    lib(const lib&) = delete;
    lib& operator=(const lib&) = delete;
};

namespace details {

struct device_impl final {
    const ::SANE_Handle m_handle;
    const std::string m_device_name;
    const lib::ptr_t m_parent;
    std::vector<const ::SANE_Option_Descriptor*> m_options;
    unsigned m_option_generation = 0;

    ~device_impl() {
        ::sane_close(m_handle);
    }
};

} // ns details

class device_option {
};

/**
 * Represents opened scanner device. While it's opened (any number of copies of this object
 * exists), the library can't be closed.
 */
class device {
public:
    struct option_iterator final {
        using difference_type = int;
        using value_type = device_option;
        using reference = device_option;
        using pointer = device_option*;
        using iterator_category = std::random_access_iterator_tag;

        option_iterator();

        reference operator*() const;
        pointer operator->() const;
        reference operator[](int) const;

        option_iterator& operator++() { ++m_pos; return *this; }
        option_iterator operator++(int) { auto r = *this; ++m_pos; return r; }
        option_iterator& operator--() { --m_pos; return *this; }
        option_iterator operator--(int) { auto r = *this; --m_pos; return r; }
        option_iterator& operator+=(int v) { m_pos += v; return *this; }
        option_iterator& operator-=(int v) { m_pos -= v; return *this; }

        friend option_iterator operator+(const option_iterator& l, int v) {
            return option_iterator{l.m_device, l.m_pos + v, l.m_size, l.m_generation};
        }
        friend option_iterator operator+(int v, const option_iterator& r) {
            return option_iterator{r.m_device, v + r.m_pos, r.m_size, r.m_generation};
        }
        friend option_iterator operator-(const option_iterator& l, int v) {
            return option_iterator{l.m_device, l.m_pos - v, l.m_size, l.m_generation};
        }
        friend option_iterator operator-(int v, const option_iterator& r) {
            return option_iterator{r.m_device, v - r.m_pos, r.m_size, r.m_generation};
        }

        int operator-(const option_iterator& r) const {
            return m_pos - r.m_pos;
        }

        bool operator==(const option_iterator& r) const {
            return m_pos == r.m_pos
                && m_size == r.m_size
                && m_generation == r.m_generation
                && m_device.lock() == r.m_device.lock();
        }

        auto operator<=>(const option_iterator& r) const {
            if (m_size != r.m_size
                || m_generation != r.m_generation
                || m_device.lock() != r.m_device.lock())
                return std::partial_ordering::unordered;
            return static_cast<std::partial_ordering>(m_pos <=> r.m_pos);
        }

    private:
        friend device;

        std::weak_ptr<details::device_impl> m_device;
        int m_pos = 0;
        int m_size = 0;
        unsigned m_generation = 0;

        option_iterator(std::weak_ptr<details::device_impl> device,
                int pos, int size, unsigned generation)
            : m_device{device}
            , m_pos{pos}
            , m_size{size}
            , m_generation{generation}
        {
        }
    };

    device() = default;

    // TODO: will we support also const ranges of options?
    std::ranges::subrange<option_iterator> get_options();

private:
    friend device lib::open_device(const char*);

    std::shared_ptr<details::device_impl> m_impl;

    device(auto p) : m_impl(std::move(p))
    {
    }
};

inline device lib::open_device(const char* name) {
    ::SANE_Handle h = {};
    details::checked_call([&name](){ return std::string{"unable to get device \""} + name + '"'; },
        &::sane_open, name, &h);
    return std::make_shared<details::device_impl>(h, name, m_inst_wptr.lock());
}

static_assert(std::bidirectional_iterator<lib::device_info_iterator>);
static_assert(std::random_access_iterator<device::option_iterator>);
static_assert(requires { std::declval<device>().get_options().empty(); });
static_assert(requires { device{}; });
// static_assert(std::is_object_v<lib::device_info> && ! requires { lib::device_info{}; });

} // ns sg_sane
