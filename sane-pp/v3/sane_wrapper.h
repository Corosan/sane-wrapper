// vi: textwidth=100
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <set>
#include <span>
#include <bitset>
#include <variant>
#include <functional>
#include <ranges>

#ifdef SANE_PP_STUB
#include <cstring>
#include <string_view>
#include <thread>
#include <chrono>
#include <initializer_list>
#endif

#include "sane_wrapper_utils.h"

#include <sane/sane.h>
/*
inline bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}
*/
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
 * a device. But it's still a design question: whether it's better to fully isolate underlying
 * library and the way data types are represented?
 */
using opt_value_t = std::variant<
    std::monostate, std::reference_wrapper<::SANE_Word>, std::span<::SANE_Word>, ::SANE_String>;

#ifdef SANE_PP_STUB

namespace details {

struct stub_option {
    ::SANE_Option_Descriptor m_d;
    std::string m_name;
    std::string m_title;
    std::string m_descr;
    std::vector<char> m_data;
    std::vector<std::string> m_str_constraint;
    std::vector<const char*> m_str_raw_constraint;

    stub_option(std::string name, std::string title, std::string descr, ::SANE_Value_Type type, ::SANE_Int cap)
        : m_d{}
        , m_name(std::move(name))
        , m_title(std::move(title))
        , m_descr(std::move(descr))
        , m_data(64) {
        m_d.name = m_name.c_str();
        m_d.title = m_title.c_str();
        m_d.desc = m_descr.c_str();
        m_d.type = type;
        m_d.cap = cap;
    }

    stub_option(const stub_option&) = delete;
    stub_option& operator=(const stub_option&) = delete;

    stub_option& set_str_constraint(std::vector<std::string> c) {
        m_d.constraint_type = SANE_CONSTRAINT_STRING_LIST;
        m_str_constraint = std::move(c);
        for (auto& s : m_str_constraint) {
            m_str_raw_constraint.push_back(s.c_str());
        }
        m_str_raw_constraint.push_back(nullptr);
        m_d.constraint.string_list = m_str_raw_constraint.data();
        return *this;
    }
};

}

#endif

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
#ifndef SANE_PP_STUB
        details::checked_call("unable to get list of devices", ::sane_get_devices, &devices, SANE_TRUE);
#else
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
        static const SANE_Device device_descrs[] = {{"dev 1", "factory 1", "dev super rk1", "mfu"},
            {"dev 2", "factory zzz", "not so super dev", "printer"}};
        static const SANE_Device* device_descr_ptrs[] = {&device_descrs[0], &device_descrs[1], nullptr};
        devices = device_descr_ptrs;
#endif
        // non-sized range could be returned instead (thus providing sentinel as the end iterator),
        // but having size property drammatically simplifies user's code
        auto d_end = devices;
        for (; *d_end; ++d_end);
        return std::ranges::subrange(devices, d_end);
    }

    /**
     * Open a scanner device specified by name. Only one scanner with specified name can exist in
     * the process - this wrapper library checks it. The call can fail even if such a name was
     * observed by enumerating entities from get_device_infos() call. A real scanner could be
     * unplugged between two calls.
     */
    device open_device(const char* name);

    ~lib() {
#ifndef SANE_PP_STUB
        ::sane_exit();
#endif
    }

private:
    inline static wptr_t m_inst_wptr;

    ::SANE_Int m_sane_ver;
    std::set<std::string> m_opened_device_names;

    lib() {
#ifndef SANE_PP_STUB
        details::checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr);
#else
        m_sane_ver = 1;
#endif
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
#ifdef SANE_PP_STUB
    using handle_t = std::vector<std::shared_ptr<details::stub_option>>;
#else
    using handle_t = ::SANE_Handle;
#endif

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
    device(device&& r)
        : m_handle{std::move(r.m_handle)}
        , m_name{std::move(r.m_name)}
        , m_deletion_cb{std::move(r.m_deletion_cb)}
        , m_cached_option_infos{std::move(r.m_cached_option_infos)} {
#ifndef SANE_PP_STUB
        r.m_handle = nullptr;
#endif
    }
    device& operator=(device&& r) {
        auto t = std::move(r);
        swap(t);
        return *this;
    }
    ~device() {
#ifdef SANE_PP_STUB
        if (! m_handle.empty())
            m_deletion_cb(m_name);
#else
        if (m_handle) {
            ::sane_close(m_handle);
            m_deletion_cb(m_name);
        }
#endif
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

#ifdef SANE_PP_STUB
    mutable handle_t m_handle;
#else
    handle_t m_handle = {};
#endif
    std::string m_name;
    deletion_cb_t m_deletion_cb; // locks the library singleton inside lambda, stored here
    mutable std::vector<const ::SANE_Option_Descriptor*> m_cached_option_infos;

    device(handle_t dev_handle, std::string name, deletion_cb_t deletion_cb)
        : m_handle{std::move(dev_handle)}
        , m_name{std::move(name)}
        , m_deletion_cb{std::move(deletion_cb)} {
    }

    const ::SANE_Option_Descriptor* get_option_info(int pos) const;
};

inline void swap(device& l, device& r) { l.swap(r); }

inline device lib::open_device(const char* name) {
    device::handle_t h = {};
    std::string sname = name;
    auto it_names = m_opened_device_names.find(sname);

    if (it_names != m_opened_device_names.end())
        throw error("already having device \"" + sname + "\" somewhere in the program");
#ifdef SANE_PP_STUB
    if (std::strcmp(name, "dev 1") == 0) {
        h = {std::make_shared<details::stub_option>("name1", "title 1", "descr 1", SANE_TYPE_INT, SANE_CAP_INACTIVE),
            std::make_shared<details::stub_option>("name2", "title 2", "descr 2", SANE_TYPE_BOOL, SANE_CAP_SOFT_SELECT),
            std::make_shared<details::stub_option>("name3", "title 33", "", SANE_TYPE_STRING, SANE_CAP_SOFT_SELECT),
            std::make_shared<details::stub_option>("name 4", "title 4", "", SANE_TYPE_STRING, SANE_CAP_SOFT_SELECT)};
        std::string_view s = "test string";
        h[0]->m_d.unit = SANE_UNIT_MM;
        h[1]->m_d.size = sizeof(::SANE_Bool);
        h[1]->m_d.unit = SANE_UNIT_DPI;
        h[2]->m_d.size = 128;
        h[2]->m_data.assign(s.begin(), s.end());
        h[3]->m_d.size = 128;
        h[3]->m_data.assign(s.begin(), s.end());
        h[3]->set_str_constraint({"val 1", "val 2", "val 3"});
    } else
        h = {std::make_shared<details::stub_option>("name1b", "title 1 b", "descr 1b", SANE_TYPE_INT, SANE_CAP_SOFT_SELECT),
            std::make_shared<details::stub_option>("name 2b", "title 2b", "descr 2bb", SANE_TYPE_BOOL, SANE_CAP_SOFT_SELECT)};
#else
    details::checked_call([&name](){ return std::string{"unable to get device \""} + name + '"'; },
        &::sane_open, name, &h);
#endif
    m_opened_device_names.insert(it_names, sname);
    return {std::move(h), std::move(sname),
        [lib_ptr = m_inst_wptr.lock()](const std::string& name) {
            lib_ptr->m_opened_device_names.erase(name);
        }};
}

inline std::ranges::subrange<device::option_iterator> device::get_option_infos() const {
    auto err = [this](){
        return "unable to get options count from device \"" + m_name + '"';
    };
#ifdef SANE_PP_STUB
    ::SANE_Int size = static_cast<::SANE_Int>(m_handle.size() + 1);
#else
    auto* zero_descr = ::sane_get_option_descriptor(m_handle, 0);
    if (! zero_descr || zero_descr->type != ::SANE_TYPE_INT)
        throw error(err());

    ::SANE_Int size = {};
    details::checked_call(err, &::sane_control_option, m_handle, 0,
        SANE_ACTION_GET_VALUE, &size, nullptr);
#endif
    return {option_iterator{this, 1}, option_iterator{this, size}};
}

inline const ::SANE_Option_Descriptor* device::get_option_info(int pos) const {
    auto tpos = static_cast<std::size_t>(pos);

    if (tpos >= m_cached_option_infos.size())
        m_cached_option_infos.resize(tpos + 1);
    if (! m_cached_option_infos[tpos]) {
#ifdef SANE_PP_STUB
        m_cached_option_infos[tpos] = &m_handle[tpos - 1]->m_d;
#else
        m_cached_option_infos[tpos] = ::sane_get_option_descriptor(m_handle, tpos);
#endif
        if (! m_cached_option_infos[tpos])
            throw error("unable to get option idx=" + std::to_string(tpos)
                + " from device \"" + m_name + '"');
    }
    return m_cached_option_infos[tpos];
}

inline opt_value_t device::get_option(int pos) const {
    void* data = {};
    auto descr = get_option_info(pos);
#ifdef SANE_PP_STUB
    data = m_handle[static_cast<std::size_t>(pos) - 1]->m_data.data();
#else
    details::checked_call([this, pos](){ return "unable to get value for option idx=" +
            std::to_string(pos) + " from device \"" + m_name + "\""; },
        &::sane_control_option, m_handle, pos, SANE_ACTION_GET_VALUE, data, nullptr);
#endif
    switch (descr->type) {
    case SANE_TYPE_BOOL:
        return {std::ref(*static_cast<::SANE_Word*>(data))};
    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
        return {std::span{static_cast<::SANE_Word*>(data), descr->size / sizeof(::SANE_Word)}};
    case SANE_TYPE_STRING:
        return {static_cast<::SANE_String>(data)};
    default:
        return {};
    }
}

inline device::set_opt_result_t device::set_option(int pos, opt_value_t val) {
    void* data = {};
    auto descr = get_option_info(pos);

    switch (descr->type) {
    case SANE_TYPE_BOOL:
        data = &get<1>(val).get();
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
    default:
        break;
    }

    ::SANE_Int flags = {};
#ifdef SANE_PP_STUB
    m_handle[static_cast<std::size_t>(pos) - 1]->m_data.assign(
        static_cast<char*>(data), static_cast<char*>(data) + descr->size);
    if (std::memcmp(static_cast<char*>(data), "test", 5) == 0) {
        m_handle.erase(m_handle.begin());
        flags |= SANE_INFO_RELOAD_OPTIONS;
    }
#else
    details::checked_call([this, pos](){ return "unable to set value for option idx=" +
            std::to_string(pos) + " from device \"" + m_name + "\""; },
        &::sane_control_option, m_handle, pos, SANE_ACTION_SET_VALUE, data, &flags);
#endif
    if (flags & SANE_INFO_RELOAD_OPTIONS)
        m_cached_option_infos.clear();

    return flags;
}

static_assert(std::bidirectional_iterator<device::option_iterator>);
static_assert(std::ranges::sized_range<decltype(std::declval<device>().get_option_infos())>);

} // ns vg_sane
