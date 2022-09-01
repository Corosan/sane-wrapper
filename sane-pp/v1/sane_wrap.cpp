#include "sane_wrap.h"

namespace sg_sane {

lib::wptr_t lib::m_inst_wptr;

device::option_iterator::option_iterator() = default;

device::option_iterator::reference device::option_iterator::operator*() const {
    const auto dev = m_device.lock();
    const auto pos = static_cast<std::size_t>(m_pos);

    if (! dev || m_generation != dev->m_option_generation)
        return device_option{};

    if (pos >= dev->m_options.size())
        dev->m_options.resize(pos + 1);

    if (! dev->m_options[pos]) {
        dev->m_options[pos] = ::sane_get_option_descriptor(dev->m_handle, static_cast<::SANE_Int>(pos));
        if (! dev->m_options[pos])
            throw error("internal SANE error while getting option idx=" + std::to_string(pos));
    }

    return device_option{};
}

device::option_iterator::pointer device::option_iterator::operator->() const {
    static device_option d;
    return &d;
}

device::option_iterator::reference device::option_iterator::operator[](int) const {
    return device_option{};
}

std::ranges::subrange<device::option_iterator> device::get_options() {
    if (! m_impl)
        return std::ranges::subrange(option_iterator{}, option_iterator{});

    ++m_impl->m_option_generation;
    m_impl->m_options.resize(1);

    auto err = [this](){
            return "unable to get options count from device \"" + m_impl->m_device_name + '"';
        };

    m_impl->m_options[0] = ::sane_get_option_descriptor(m_impl->m_handle, 0);
    if (! m_impl->m_options[0] || m_impl->m_options[0]->type != ::SANE_TYPE_INT)
        throw error(err());

    ::SANE_Int size = {};
    details::checked_call(err, &::sane_control_option, m_impl->m_handle, 0,
        SANE_ACTION_GET_VALUE, &size, nullptr);

    return std::ranges::subrange(option_iterator{m_impl, 0, size, m_impl->m_option_generation},
        option_iterator{m_impl, size, size, m_impl->m_option_generation});
}

}
