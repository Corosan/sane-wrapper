#include <iostream>
#include <stdexcept>
#include <memory>
#include <vector>

#include <iterator>
#include <ranges>
#include <type_traits>

#include <sane/sane.h>

constexpr bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}

namespace sg_sane {

class error : public std::runtime_error {
public:
    explicit error(SANE_Status status)
        : runtime_error(::sane_strstatus(status)) {
    }
};

class lib final {
    template <class ... Parms, class ... Args>
    static void checked_call(::SANE_Status (*f)(Parms ...), Args&& ... args) {
        if (auto status = (*f)(std::forward<Args>(args) ...); status != SANE_STATUS_GOOD)
            throw error(status);
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
        const pointer* operator->() const {
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
            checked_call(::sane_get_devices, &m_devices, SANE_TRUE);
        }

        return std::ranges::subrange(
            device_info_iterator{m_devices}, std::default_sentinel);
    }

    auto get_device_infos_raw(bool reload = false) {
        if (! m_devices || reload) {
            m_devices = {};
            checked_call(::sane_get_devices, &m_devices, SANE_TRUE);
        }

        return std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>(
            &m_devices[0], std::default_sentinel);
    }

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

    lib() { checked_call(&::sane_init, &m_sane_ver, nullptr); }

    lib(const lib&) = delete;
    lib& operator=(const lib&) = delete;
};

lib::wptr_t lib::m_inst_wptr;

} // sg_sane

//struct Dt {
//    const char* p;
//};
//
//struct WDt {
//    Dt* m_p;
//    auto p() const { return m_p->p; }
//};

//constexpr bool operator==(std::default_sentinel_t, Dt** r) {
//    return !*r;
//};

//struct WIterator {
//    using difference_type = std::iterator_traits<Dt*>::difference_type;
//    using value_type = WDt;
//    using reference = WDt;
//    using pointer = WDt*;
//    using iterator_category = std::bidirectional_iterator_tag;
//    Dt** m_inner;
//
//    value_type operator*() const { return WDt{*m_inner}; }
//    const value_type* operator->() const { static WDt t; t.m_p = *m_inner; return &t; }
//    WIterator& operator++() { ++m_inner; return *this; }
//    WIterator operator++(int) { auto r = *this; ++m_inner; return r; }
//    WIterator& operator--() { --m_inner; return *this; }
//    WIterator operator--(int) { auto r = *this; --m_inner; return r; }
//
//    bool operator==(const WIterator& r) const { return m_inner == r.m_inner; }
//};
//
//constexpr bool operator==(const WIterator& w, std::default_sentinel_t) {
//    return !*w.m_inner;
//};
//
//static_assert(std::is_convertible_v<std::iterator_traits<WIterator>::iterator_category, std::bidirectional_iterator_tag>);
//
//auto get_d() {
//    static Dt vals[] = {"a1", "s2"};
//    static Dt* data[] = {&vals[0], &vals[1], nullptr};
//    return std::ranges::subrange(WIterator{&data[0]}, std::default_sentinel);
//}

int main() {
    auto sane = sg_sane::lib::instance();

    for (auto d : sane->get_device_infos()) {
        std::cout << d.name() << std::endl;
    }

    for (auto p : sane->get_device_infos_raw()) {
        std::cout << p->name << std::endl;
    }
}
