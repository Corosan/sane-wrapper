#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <iterator>
#include <ranges>
#include <functional>

#include <sane/sane.h>

constexpr bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}

namespace sg_sane {

class error : public std::runtime_error {
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
    explicit error(std::string msg_prefix, ::SANE_Status status)
        : runtime_error(get_msg(std::move(msg_prefix), status))
    {
    }
};

class device_info;

/**
 * Represents Sane library which is a singleton for the whole process. So the object can't be
 * created via constructor but instead is returned by calling instance() method. Only one object
 * exists at any time.
 */
class lib final {
    template <class Msg, class ... Parms, class ... Args>
    static void checked_call(Msg&& msg, ::SANE_Status (*f)(Parms ...), Args&& ... args) {
        if (auto status = (*f)(std::forward<Args>(args) ...); status != SANE_STATUS_GOOD) {
            if constexpr (std::is_convertible_v<std::decay_t<Msg>, std::string>) {
                throw error(std::forward<Msg>(msg), status);
            } else if constexpr (std::is_invocable_v<Msg>) {
                throw error(std::invoke(std::forward<Msg>(msg)), status);
            } else
                static_assert(!sizeof(Msg));
        }
    }

public:
    using ptr_t = std::shared_ptr<lib>;
    using wptr_t = std::weak_ptr<lib>;

    struct device_info_iterator;

    // TODO: will do something with dangling pointers? Though it should be
    // short living object. Whether it adds any additional safety or it's
    // simpler to return just raw ::SANE_Device pointers on iterating?
    struct device_info_view final {
        const char* name() const { return m_impl->name; }
        const char* vendor() const { return m_impl->vendor; }
        const char* model() const { return m_impl->model; }
        const char* type() const { return m_impl->type; }
    private:
        friend device_info_iterator;
        const ::SANE_Device* m_impl;

        device_info_view() = default;
        device_info_view(const ::SANE_Device* impl) : m_impl(impl)
        {
        }
    };

    struct device_info_iterator {
        using difference_type = std::iterator_traits<SANE_Device**>::difference_type;
        using value_type = device_info_view;
        using reference = device_info_view;
        using pointer = device_info_view*;
        using iterator_category = std::bidirectional_iterator_tag;

        reference operator*() const { return *m_devices; }
        const pointer operator->() const {
            static device_info_view t;
            t.m_impl = *m_devices;
            return &t;
        }
        auto& operator++() { ++m_devices; return *this; }
        auto operator++(int) { auto r = *this; ++m_devices; return r; }
        auto& operator--() { --m_devices; return *this; }
        auto operator--(int) { auto r = *this; --m_devices; return r; }

        constexpr bool operator==(const device_info_iterator& r) const {
            return m_devices == r.m_devices;
        }
        friend constexpr bool operator==(const device_info_iterator& l, std::default_sentinel_t) {
            return ! *l.m_devices;
        }

    private:
        friend lib;
        const ::SANE_Device** m_devices;

        device_info_iterator(const ::SANE_Device** devices) : m_devices(devices)
        {
        }
    };

    auto get_device_infos(bool reload = false) {
        if (! m_devices || reload) {
            m_devices = {};
            checked_call("unable to get list of devices", ::sane_get_devices, &m_devices, SANE_TRUE);
        }

        return std::ranges::subrange(
            device_info_iterator{m_devices}, std::default_sentinel);
    }

    auto get_device_infos_raw(bool reload = false) {
        if (! m_devices || reload) {
            m_devices = {};
            checked_call("unable to get list of devices", ::sane_get_devices, &m_devices, SANE_TRUE);
        }

        return std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>(
            &m_devices[0], std::default_sentinel);
    }

    device_info open_device(const char* name);

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

    lib() { checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr); }

    lib(const lib&) = delete;
    lib& operator=(const lib&) = delete;
};

/**
 * Represents opened scanner device. While it's opened (any number of copies of this object
 * exists), the library can't be closed.
 */
class device_info {
private:
    friend device_info lib::open_device(const char*);

    struct device_impl final {
        ::SANE_Handle m_handle;
        lib::ptr_t m_parent;

        ~device_impl() {
            ::sane_close(m_handle);
        }
    };

    std::shared_ptr<device_impl> m_impl;

    device_info(const auto& p) : m_impl(p)
    {
    }
};

inline device_info lib::open_device(const char* name) {
    ::SANE_Handle h = {};
    checked_call([&name](){ return std::string{"unable to get device \""} + name + '"'; },
        &::sane_open, name, &h);
    return std::make_shared<device_info::device_impl>(h, m_inst_wptr.lock());
}

} // ns sg_sane
