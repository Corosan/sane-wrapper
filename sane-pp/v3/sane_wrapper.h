// vi: textwidth=100
#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <set>
#include <span>
#include <bitset>
#include <variant>
#include <functional>
#include <ranges>

#include "sane_wrapper_utils.h"

#include <sane/sane.h>

inline bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}

namespace vg_sane {

class device;

/**
 * A holder of various Sane types. Indexed as:
 *   [0] (std::monostate) - for SANE_TYPE_BUTTON, SANE_TYPE_GROUP
 *   [1] (::SANE_Word&) - for SANE_TYPE_BOOL
 *   [2] (std::span<::SANE_Word>) - for SANE_TYPE_INT, SANE_TYPE_FIXED (they could be arrays)
 *   [3] (::SANE_String) - for SANE_TYPE_STRING
 * Underlying types are exposed here via C++ wrapper public interface for increasing efficiency
 * because the C library can slighly modify an option value even while a caller wants to set it for
 * a device.
 */
using opt_value_t = std::variant<
    std::monostate, std::reference_wrapper<::SANE_Word>, std::span<::SANE_Word>, ::SANE_String>;

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
    enum class set_opt_result_flags : char {
        value_inexact = 0,
        reload_opts = 1,
        reload_params = 2,
        last
    };

    using set_opt_result_t = std::bitset<static_cast<std::size_t>(set_opt_result_flags::last)>;

    struct option_iterator final {
        using value_type = std::pair<int, const ::SANE_Option_Descriptor*>;

        option_iterator() {} // option_iterator() = default; - should be enough really

        value_type operator*() const {
            return {m_pos, m_parent_device->get_option_info(m_pos)};
        }
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
        , m_deletion_cb{std::move(r.m_deletion_cb)}
        , m_cached_option_infos{std::move(r.m_cached_option_infos)} {
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
        swap(m_cached_option_infos, r.m_cached_option_infos);
    }

    const std::string& name() const { return m_name; }
    std::ranges::subrange<option_iterator> get_option_infos() const;
    opt_value_t get_option(int pos) const;
    set_opt_result_t set_option(int pos, opt_value_t val);

private:
    using deletion_cb_t = std::function<void(const std::string&)>;

    ::SANE_Handle m_handle = {};
    std::string m_name;
    deletion_cb_t m_deletion_cb; // locks the library singleton inside lambda, stored here
    mutable std::vector<const ::SANE_Option_Descriptor*> m_cached_option_infos;

    device(::SANE_Handle dev_handle, std::string name, deletion_cb_t deletion_cb)
        : m_handle{dev_handle}
        , m_name{std::move(name)}
        , m_deletion_cb{std::move(deletion_cb)} {
    }

    const ::SANE_Option_Descriptor* get_option_info(int pos) const;
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

const ::SANE_Option_Descriptor* device::get_option_info(int pos) const {
    auto tpos = static_cast<std::size_t>(pos);

    if (tpos >= m_cached_option_infos.size())
        m_cached_option_infos.resize(tpos + 1);
    if (! m_cached_option_infos[tpos]) {
        m_cached_option_infos[tpos] = ::sane_get_option_descriptor(m_handle, tpos);
        if (! m_cached_option_infos[tpos])
            throw error("unable to get option idx=" + std::to_string(tpos)
                + " from device \"" + m_name + '"');
    }
    return m_cached_option_infos[tpos];
}

opt_value_t device::get_option(int pos) const {
    void* data = {};
    auto descr = get_option_info(pos);

    details::checked_call([this, pos](){ return "unable to get value for option idx=" +
            std::to_string(pos) + " from device \"" + m_name + "\""; },
        &::sane_control_option, m_handle, pos, SANE_ACTION_GET_VALUE, data, nullptr);

    switch (descr->type) {
    case SANE_TYPE_BOOL:
        return {*static_cast<::SANE_Word*>(data)};
    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
        return {std::span{static_cast<::SANE_Word*>(data), descr->size / sizeof(::SANE_Word)}};
    case SANE_TYPE_STRING:
        return {static_cast<::SANE_String>(data)};
    default:
        return {};
    }
}

device::set_opt_result_t device::set_option(int pos, opt_value_t val) {
    void* data = {};
    auto descr = get_option_info(pos);

    switch (descr->type) {
    case SANE_TYPE_BOOL:
        data = &get<1>(val);
        break;
    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
        if (get<2>(val).size() != descr->size / sizeof(::SANE_Word))
            throw error("invalid size of [array] value to set into option idx="
                + std::to_string(pos) + " in device \"" + m_name + "\"");
        data = get<2>(val).data();
        break;
    case SANE_TYPE_STRING:
        // Skip to check max size of provided buffer because from a doc: "The
        // only exception to this rule is that when setting the value of a
        // string option, the string pointed to by argument v may be shorter
        // since the backend will stop reading the option value upon
        // encountering the first NUL terminator in the string."
        data = get<3>(val);
        break;
    }

    ::SANE_Int flags = {};
    details::checked_call([this, pos](){ return "unable to set value for option idx=" +
            std::to_string(pos) + " from device \"" + m_name + "\""; },
        &::sane_control_option, m_handle, pos, SANE_ACTION_SET_VALUE, data, &flags);

    if (flags & SANE_INFO_RELOAD_OPTIONS)
        m_cached_option_infos.clear();

    return flags;
}

static_assert(std::bidirectional_iterator<device::option_iterator>);
static_assert(std::ranges::sized_range<decltype(std::declval<device>().get_option_infos())>);

} // ns vg_sane
