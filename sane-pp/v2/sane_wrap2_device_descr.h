#include <utility>
#include <string>

#include <sane/sane.h>

namespace vg_sane {

class lib;
class device_descr_iterator;

class device_descr final {
public:
    device_descr() = default;
    device_descr(const device_descr&);
    device_descr(device_descr&&);
    device_descr& operator=(const device_descr& r) {
        auto t = r;
        swap(t);
        return *this;
    }
    device_descr& operator=(device_descr&& r) {
        auto t = std::move(r);
        swap(t);
        return *this;
    }

    const char* name() const {
        return m_sane_ptr ? m_sane_ptr->name : m_name.c_str();
    }
    const char* vendor() const {
        return m_sane_ptr ? m_sane_ptr->vendor : m_vendor.c_str();
    }
    const char* model() const {
        return m_sane_ptr ? m_sane_ptr->model : m_model.c_str();
    }
    const char* type() const {
        return m_sane_ptr ? m_sane_ptr->type : m_type.c_str();
    }

    void swap(device_descr&);

private:
    friend device_descr_iterator;

    const ::SANE_Device* m_sane_ptr{};
    std::string m_name;
    std::string m_vendor;
    std::string m_model;
    std::string m_type;

    device_descr(const ::SANE_Device* sane_ptr) : m_sane_ptr(sane_ptr) {}
};

class device_descr_iterator final {
public:
    using difference_type = std::iterator_traits<SANE_Device**>::difference_type;
    using value_type = device_descr;
    using reference = device_descr;
    using pointer = const device_descr*;
    using iterator_category = std::bidirectional_iterator_tag;

    device_descr_iterator() = default;

    reference operator*() const {
        return *m_sane_devices;
    }
    pointer operator->() const {
        static device_descr d;
        d.m_sane_ptr = *m_sane_devices;
        return &d;
    }

    auto& operator++() { ++m_sane_devices; return *this; }
    auto operator++(int) { auto r = *this; ++m_sane_devices; return r; }
    auto& operator--() { --m_sane_devices; return *this; }
    auto operator--(int) { auto r = *this; --m_sane_devices; return r; }

    bool operator==(const device_descr_iterator& r) const {
        return m_sane_devices == r.m_sane_devices;
    }
    friend bool operator==(const device_descr_iterator& l, std::default_sentinel_t) {
        return ! *l.m_sane_devices;
    }

private:
    friend lib;
    static inline const ::SANE_Device* m_sane_devices_end = nullptr;
    const ::SANE_Device** m_sane_devices = &m_sane_devices_end;

    device_descr_iterator(const ::SANE_Device** sane_devices) : m_sane_devices(sane_devices) {}
};

inline void swap(device_descr& l, device_descr& r) { l.swap(r); }

static_assert(std::bidirectional_iterator<device_descr_iterator>);

} // ns vg_sane

namespace std {

inline void swap(::vg_sane::device_descr& l, ::vg_sane::device_descr& r) { l.swap(r); }

} // ns std
