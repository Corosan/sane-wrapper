#include "sane_wrap2.h"
#include "sane_wrap2_utils.h"

#include <utility>

namespace vg_sane {

std::unique_ptr<lib> lib::m_instance;

lib::lib() {
    ::SANE_Int m_sane_ver{};
    details::checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr);
}

lib::~lib() {
    ::sane_exit();
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

device::~device() {
    if (m_handle)
        ::sane_close(m_handle);
}

} // ns vg_sane
