#pragma once

#include <memory>
#include <utility>
#include <ranges>
#include <set>
#include <functional>

#include "sane_wrapper_utils.h"

#include <sane/sane.h>

inline bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}

namespace vg_sane {

class device;

/**
 * Represents Sane library wrapper which is a singleton for the whole process. So the object can't
 * be created via constructor but instead is returned as a smart pointer by calling instance()
 * method. Only one object exists at any time. Any device object got from it internally saves this
 * singleton from deletion even if no other pointers to the library exist.
 */
class lib final {
public:
    using ptr_t = std::shared_ptr<lib>;
    using wptr_t = std::weak_ptr<lib>;

    static ptr_t instance() {
        if (auto p = m_inst_wptr.lock())
            return p;
        auto p = ptr_t{new lib};
        m_inst_wptr = p;
        return p;
    }

    /**
     * @returns forward-iterable range of Sane device infos. No caching, each call to this method
     *    makes a call to underlying C library.
     */
    auto get_device_infos() const {
        const ::SANE_Device** devices;
        details::checked_call("unable to get list of devices", ::sane_get_devices, &devices, SANE_TRUE);
        return std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>(
            devices, std::default_sentinel);
    }

    /**
     * Open a scanner device specified by name. Only one scanner with specified name can exist in
     * the process - this wrapper library checks it. The call can fail even if such a name was
     * observed by enumerating entities from get_device_infos() call. A real scanner could be
     * unplugged between two calls.
     */
    device open_device(const char* name);

    ~lib() {
        ::sane_exit();
    }

private:
    inline static wptr_t m_inst_wptr;

    ::SANE_Int m_sane_ver;
    std::set<std::string> m_opened_device_names;

    lib() {
        details::checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr);
    }

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
    struct option_iterator final {
        using value_type = const ::SANE_Option_Descriptor*;

        option_iterator() {} // option_iterator() = default; - should be enough really

        value_type operator*() const;
        option_iterator& operator++() { ++m_pos; return *this; }
        option_iterator operator++(int) { auto t = *this; ++m_pos; return t; }
        option_iterator& operator--() { --m_pos; return *this; }
        option_iterator operator--(int) { auto t = *this; --m_pos; return t; }
        int operator-(const option_iterator& r) const { return m_pos - r.m_pos; }
        auto operator<=>(const option_iterator&) const = default;

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
    device(device&& r)
        : m_handle{r.m_handle}
        , m_name{std::move(r.m_name)}
        , m_deletion_cb{std::move(r.m_deletion_cb)} {
        r.m_handle = nullptr;
    }
    device& operator=(device&& r) {
        auto t = std::move(r);
        swap(t);
        return *this;
    }
    ~device() {
        if (m_handle) {
            ::sane_close(m_handle);
            m_deletion_cb(m_name);
        }
    }

    void swap(device& r) {
        using std::swap;
        swap(m_handle, r.m_handle);
        swap(m_name, r.m_name);
        swap(m_deletion_cb, r.m_deletion_cb);
    }

    const std::string& name() const { return m_name; }

    std::ranges::subrange<option_iterator> get_option_infos() const;

private:
    using deletion_cb_t = std::function<void(const std::string&)>;

    ::SANE_Handle m_handle = {};
    std::string m_name;
    deletion_cb_t m_deletion_cb; // locks the library singleton inside lambda, stored here

    device(::SANE_Handle dev_handle, std::string name, deletion_cb_t deletion_cb)
        : m_handle{dev_handle}
        , m_name{std::move(name)}
        , m_deletion_cb{std::move(deletion_cb)} {
    }
};

inline void swap(device& l, device& r) { l.swap(r); }

device lib::open_device(const char* name) {
    ::SANE_Handle h = {};
    std::string sname = name;
    auto it_names = m_opened_device_names.find(sname);

    if (it_names != m_opened_device_names.end())
        throw error("already having device \"" + sname + "\" somewhere in the program");

    details::checked_call([&name](){ return std::string{"unable to get device \""} + name + '"'; },
        &::sane_open, name, &h);
    m_opened_device_names.insert(it_names, sname);
    return {h, std::move(sname),
        [lib_ptr = m_inst_wptr.lock()](const std::string& name) {
            lib_ptr->m_opened_device_names.erase(name);
        }};
}

std::ranges::subrange<device::option_iterator> device::get_option_infos() const {
    auto err = [this](){
        return "unable to get options count from device \"" + m_name + '"';
    };

    auto* zero_descr = ::sane_get_option_descriptor(m_handle, 0);
    if (! zero_descr || zero_descr->type != ::SANE_TYPE_INT)
        throw error(err());

    ::SANE_Int size = {};
    details::checked_call(err, &::sane_control_option, m_handle, 0,
        SANE_ACTION_GET_VALUE, &size, nullptr);

    return {option_iterator{this, 1}, option_iterator{this, size}};
}

auto device::option_iterator::operator*() const -> value_type {
    auto* descr = ::sane_get_option_descriptor(m_parent_device->m_handle, m_pos);
    if (! descr)
        throw error("unable to get option idx=" + std::to_string(m_pos)
            + " from device \"" + m_parent_device->m_name + '"');
    return descr;
}

static_assert(std::bidirectional_iterator<device::option_iterator>);

} // ns vg_sane
