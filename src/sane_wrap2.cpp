#include "sane_wrap2.h"
#include "sane_wrap2_utils.h"

#include <cstdlib>
#include <utility>

namespace vg_sane {

lib* lib::instance() {
    static lib inst;
    return &inst;
}

lib::lib() {
    details::checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr);
}

lib::~lib() {
    // SANE library crashes if when calling this method after going out of main()
    // ::sane_exit();
}

std::ranges::subrange<device_descr_iterator, std::default_sentinel_t>
lib::get_device_descrs(bool reload) {
    if (! m_sane_devices || reload) {
        m_sane_devices = {};
        details::checked_call("unable to get list of devices", ::sane_get_devices, &m_sane_devices, SANE_TRUE);
    }

    return {device_descr_iterator{m_sane_devices}, std::default_sentinel};
}

std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>
lib::get_device_descrs_raw(bool reload) {
    if (! m_sane_devices || reload) {
        m_sane_devices = {};
        details::checked_call("unable to get list of devices", ::sane_get_devices, &m_sane_devices, SANE_TRUE);
    }

    return std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>(
        &m_sane_devices[0], std::default_sentinel);
}

device lib::open_device(const char* name) {
    ::SANE_Handle h = {};
    details::checked_call([&name](){ return std::string{"unable to get device \""} + name + '"'; },
        &::sane_open, name, &h);
    return {h, name};
}

void device::swap(device& r) {
    using std::swap;
    swap(m_handle, r.m_handle);
    swap(m_name, r.m_name);
}

auto device::get_option_descrs() const -> std::ranges::subrange<option_iterator> {
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

auto device::option_iterator::operator[](int v) const -> reference_type {
    auto* descr = ::sane_get_option_descriptor(m_parent->m_handle, m_pos + v);
    if (! descr)
        throw error("unable to get option description idx="
            + std::to_string(m_pos) + " for device\"" + m_parent->m_name + '"');
    return {m_pos, descr};
}

auto device::option_iterator::operator->() const -> pointer_type {
    static value_type v;
    v.first = m_pos;
    v.second = ::sane_get_option_descriptor(m_parent->m_handle, m_pos);
    if (! v.second)
        throw error("unable to get option description idx="
            + std::to_string(m_pos) + " for device\"" + m_parent->m_name + '"');
    return &v;
}

} // ns vg_sane
