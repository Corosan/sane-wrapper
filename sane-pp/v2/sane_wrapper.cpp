#include "sane_wrapper.h"

#include "sane_wrapper_utils.h"

#include <cassert>
#include <type_traits>
#include <tuple>
#include <list>
#include <mutex>

namespace vg_sane {

class lib::impl {
public:
    ::SANE_Int m_lib_version = {};
    std::function<void()> m_worker_notifier;
    std::function<void()> m_api_notifier;

    void send_to_core(fwd_messages_t val);
    void worker_dispatch();
    void api_dispatch();

private:
    std::list<fwd_messages_t> m_fwd_queue;
    std::mutex m_fwd_queue_lock;

    std::list<std::function<void()>> m_bkwd_queue;
    std::mutex m_bkwd_queue_lock;

    template <typename M>
    void handle_api_call(M&& msg);

    messages::enumerate_result handle_api_call_impl(messages::enumerate_args&&);
};

void lib::impl::api_dispatch() {
    std::list<std::function<void()>> tmp_list;

    {
        std::lock_guard guard{m_bkwd_queue_lock};
        tmp_list.swap(m_bkwd_queue);
    }

    for (auto& cb : tmp_list)
        cb();
};

void lib::impl::worker_dispatch() {
    std::list<fwd_messages_t> tmp_list;

    {
        std::lock_guard guard{m_fwd_queue_lock};
        tmp_list.swap(m_fwd_queue);
    }

    for (fwd_messages_t& m : tmp_list)
        std::visit(
            [this]<typename M>(M&& msg){ handle_api_call(std::forward<M>(msg)); },
            std::move(m));
}

template <typename M>
void lib::impl::handle_api_call(M&& msg) {
    std::function<void()> carry_result_cb;

    try {
        auto res = handle_api_call_impl(std::get<2>(std::forward<M>(msg)));
        carry_result_cb = [weak_obj = std::get<0>(msg), ptr = std::get<1>(msg), res = std::move(res)](){
                if (auto p = weak_obj.lock())
                    (p.get()->*ptr)(std::move(res));
            };
    } catch (...) {
        carry_result_cb =
            [weak_obj = std::get<0>(msg), ptr = std::get<1>(msg), exc = std::current_exception()]() {
                    if (auto p = weak_obj.lock())
                        (p.get()->*ptr)(exc);
                };
    };

    {
        std::lock_guard guard{m_bkwd_queue_lock};
        m_bkwd_queue.push_back(std::move(carry_result_cb));
    }

    if (m_api_notifier)
        m_api_notifier();
}

void lib::impl::send_to_core(fwd_messages_t val) {
    {
        std::lock_guard guard{m_fwd_queue_lock};
        m_fwd_queue.push_back(std::move(val));
    }

    if (m_worker_notifier)
        m_worker_notifier();
}

messages::enumerate_result lib::impl::handle_api_call_impl(messages::enumerate_args&&) {
    return {};
}

//-----------------------------------------------------------------------------

std::weak_ptr<lib> lib::m_instance;

std::shared_ptr<lib> lib::instance() {
    if (auto p = m_instance.lock())
        return p;

    std::shared_ptr<lib> p{new lib};
    m_instance = p;
    return p;
}

lib::lib()
    : m_impl{std::make_unique<impl>()} {
#ifndef SANE_PP_STUB
    details::checked_call("unable to initialize library", &::sane_init, &m_impl->m_lib_version, nullptr);
#endif
}

lib::~lib() {
#ifndef SANE_PP_STUB
    ::sane_exit();
#endif
}

void lib::set_worker_notifier(std::function<void()> val) {
    m_impl->m_worker_notifier = val;
}

void lib::set_api_notifier(std::function<void()> val) {
    m_impl->m_api_notifier = val;
}

void lib::worker_dispatch() {
    m_impl->worker_dispatch();
}

//-----------------------------------------------------------------------------

void device_enumerator::start_enumerate() {
    if (! m_enabled)
        return;

    auto lib_ptr = lib::weak_instance().lock();
    assert(lib_ptr);
    if (! lib_ptr)
        return;

    lib_ptr->m_impl->send_to_core(std::make_tuple(
        weak_from_this(), &device_enumerator::handle_result, messages::enumerate_args{}));

    m_enabled = false;
    if (m_events_receiver)
        m_events_receiver->enabled(m_enabled);
}

void device_enumerator::handle_result(std::variant<messages::enumerate_result, std::exception_ptr> val) {
    m_enabled = true;

    if (m_events_receiver) {
        if (auto exc = std::get_if<std::exception_ptr>(&val))
            m_events_receiver->unhandled_exception(*exc);

        m_events_receiver->list_changed();
    }
}

} // ns vg_sane
