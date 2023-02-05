#include "sane_wrapper.h"

#include "sane_wrapper_utils.h"

#include <stdexcept>

namespace vg_sane {

lib::wptr_t lib::m_inst_wptr;

// Internal interface of the library for objects inside this domain
struct lib::lib_internal final {
    template <typename F>
    void log(LogLevel level, F&& msgProducer) {
        if (m_parent->m_logger_sink)
            m_parent->m_logger_sink(level, std::forward<F>(msgProducer)());
    }

private:
    friend lib;

    lib* m_parent;

    lib_internal(lib* parent) : m_parent{parent} {}
};

lib::ptr_t lib::instance() {
    if (auto p = m_inst_wptr.lock())
        return p;
    auto p = ptr_t{new lib};
    m_inst_wptr = p;
    return p;
}

lib::lib()
    : m_internal_iface{new lib_internal{this}} {
#ifndef SANE_PP_STUB
    details::checked_call("unable to initialize library", &::sane_init, &m_sane_ver, nullptr);
#else
    m_sane_ver = 1;
#endif
}

lib::~lib() {
#ifndef SANE_PP_STUB
    ::sane_exit();
#endif
}

devices_t lib::get_device_infos() const {
    const ::SANE_Device** devices;
#ifndef SANE_PP_STUB
    details::checked_call("unable to get list of devices", ::sane_get_devices, &devices, SANE_TRUE);
#else
    static const SANE_Device device_descrs[] = {{"dev 1", "factory 1", "dev super rk1", "mfu"},
        {"dev 2", "factory zzz", "not so super dev", "printer"}};
    static const SANE_Device* device_descr_ptrs[] = {&device_descrs[0], &device_descrs[1], nullptr};
    devices = device_descr_ptrs;
#endif
    // non-sized range could be returned instead (thus providing sentinel as an end iterator),
    // but having size property drammatically simplifies user's code
    auto d_end = devices;
    for (; *d_end; ++d_end);
    return std::ranges::subrange(devices, d_end);
}

device lib::open_device(const char* name) {
    device::handle_t h = {};
    std::string sname = name;
    auto it_names = m_opened_device_names.find(sname);

    if (it_names != m_opened_device_names.end())
        throw error("already having device \"" + sname + "\" somewhere in the program");
#ifdef SANE_PP_STUB
    if (std::strcmp(name, "dev 1") == 0) {
        h = {std::make_shared<details::stub_option>("n0", "int sample", "", SANE_TYPE_INT, SANE_CAP_SOFT_SELECT, 1, SANE_UNIT_MM),
             std::make_shared<details::stub_option>("n1", "int list sample", "", SANE_TYPE_INT, SANE_CAP_SOFT_SELECT, 3, SANE_UNIT_BIT),
             std::make_shared<details::stub_option>("n2", "fixed sample", "", SANE_TYPE_FIXED, SANE_CAP_SOFT_SELECT),
             std::make_shared<details::stub_option>("n3", "fixed list sample", "", SANE_TYPE_FIXED, SANE_CAP_SOFT_SELECT, 3),
             std::make_shared<details::stub_option>("n4", "str", "", SANE_TYPE_STRING, SANE_CAP_SOFT_SELECT, 32),
             std::make_shared<details::stub_option>("n5", "btn", "", SANE_TYPE_BUTTON, SANE_CAP_SOFT_SELECT)};
        h[0]->value<::SANE_Word>() = 2;
        h[0]->set_int_range_constraint({-6, 6000, 2});
        h[1]->values<::SANE_Word>() = {1, 2, 3};
        h[1]->set_int_range_constraint({-10, 10, 1});
        h[2]->value<::SANE_Fixed>() = 1 << SANE_FIXED_SCALE_SHIFT;
        h[2]->set_int_range_constraint({0, 10 << SANE_FIXED_SCALE_SHIFT, 1 << (SANE_FIXED_SCALE_SHIFT - 1)});
        h[3]->values<::SANE_Fixed>() = {1 << SANE_FIXED_SCALE_SHIFT, 2 << SANE_FIXED_SCALE_SHIFT, 5 << (SANE_FIXED_SCALE_SHIFT - 1)};
        h[4]->str() = "test string";
    } else {
        // No properties
    }
#else
    details::checked_call([&name](){ return std::string{"unable to get device \""} + name + '"'; },
        &::sane_open, name, &h);
#endif

    m_opened_device_names.insert(it_names, sname);

    return {std::move(h), std::move(sname), m_internal_iface.get(),
        [lib_ptr = m_inst_wptr.lock()](const std::string& name) {
            lib_ptr->m_opened_device_names.erase(name);
        }};
}

//-----------------------------------------------------------------------------

device::device(handle_t dev_handle, std::string name, lib::lib_internal* lib_int, deletion_cb_t deletion_cb)
    : m_handle{std::move(dev_handle)}
    , m_name{std::move(name)}
    , m_lib_internal{lib_int}
    , m_deletion_cb{std::move(deletion_cb)} {

    m_lib_internal->log(LogLevel::Info, [this](){ return "opened device \"" + m_name + '"'; });
}

device::device(device&& r)
    : m_handle{std::move(r.m_handle)}
    , m_name{std::move(r.m_name)}
    , m_lib_internal{r.m_lib_internal}
    , m_deletion_cb{std::move(r.m_deletion_cb)} {
#ifndef SANE_PP_STUB
    r.m_handle = nullptr;
#endif
}

device::~device() {
    if (! m_name.empty())
        m_lib_internal->log(LogLevel::Info, [this]() { return "closed device \"" + m_name + '"'; });
#ifdef SANE_PP_STUB
    if (! m_name.empty())
        m_deletion_cb(m_name);
#else
    if (m_handle) {
        ::sane_close(m_handle);
        m_deletion_cb(m_name);
    }
#endif
}

std::ranges::subrange<device::option_iterator> device::get_option_infos() const {
    auto err = [this](){
        return "unable to get options count from device \"" + m_name + '"';
    };
#ifdef SANE_PP_STUB
    ::SANE_Int size = static_cast<::SANE_Int>(m_handle.size() + 1);
#else
    ::SANE_Int size = 1;

    if (m_handle) {
        auto* zero_descr = ::sane_get_option_descriptor(m_handle, 0);
        if (! zero_descr || zero_descr->type != ::SANE_TYPE_INT)
            throw error(err());

        details::checked_call(err, &::sane_control_option, m_handle, 0,
            SANE_ACTION_GET_VALUE, &size, nullptr);
    }
#endif
    return {option_iterator{this, 1}, option_iterator{this, size}};
}

const ::SANE_Option_Descriptor* device::get_option_info(int pos) const {
#ifdef SANE_PP_STUB
    return &m_handle[pos-1]->m_d;
#else
    if (auto p = ::sane_get_option_descriptor(m_handle, pos))
        return p;

    throw error("unable to get option idx=" + std::to_string(pos)
        + " from device \"" + m_name + '"');
#endif
}

opt_value_t device::get_option(int pos) const {
    void* data = {};
    auto descr = get_option_info(pos);
#ifdef SANE_PP_STUB
    data = m_handle[static_cast<std::size_t>(pos) - 1]->m_data.data();
#else
    m_option_data_buffer.resize(descr->size);
    data = m_option_data_buffer.data();
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

device::set_opt_result_t device::set_option(int pos, opt_value_t val) {
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
    if (data) {
        m_handle[pos-1]->m_data.assign(
            static_cast<char*>(data), static_cast<char*>(data) + descr->size);

        if (m_name == "dev 1" && pos == 2) {
            auto data = reinterpret_cast<::SANE_Word*>(m_handle[1]->m_data.data());
            if (*data == 10)
                throw std::runtime_error("text exception while setting the value");
            *(data + 2) = 2;
        }

        if (std::memcmp(static_cast<char*>(data), "test", 5) == 0) {
            m_handle.erase(m_handle.begin());
            flags |= SANE_INFO_RELOAD_OPTIONS;
        }
    }
#else
    details::checked_call([this, pos](){ return "unable to set value for option idx=" +
            std::to_string(pos) + " from device \"" + m_name + "\""; },
        &::sane_control_option, m_handle, pos, SANE_ACTION_SET_VALUE, data, &flags);
#endif

    return flags;
}

void device::start_scanning(std::function<void()> cb) {
    if (m_scanning_state != ScanningState::Idle)
        throw std::logic_error("trying to start scanning on \"" + m_name + "\" device "
            "while the scanning is in progress");

    m_lib_internal->log(LogLevel::Info, [this](){ return "start scanning on device \"" + m_name + '"'; });
    m_use_internal_waiter = !cb;
    if (m_use_internal_waiter)
        cb = [this](){ m_internal_state_waiting.notify_all(); };

    m_scanning_state_notifier = std::move(cb);
    m_last_scanning_error = {};
    m_scanning_params = {};
    m_chunks.clear();

    m_scanning_state = ScanningState::Initializing;
    try {
        m_scanning_thread = std::jthread([this](std::stop_token s){ do_scanning(std::move(s)); });
    } catch (...) {
        m_scanning_state = ScanningState::Idle;
        throw;
    }
}

void device::cancel_scanning(bool fast) {
    if (m_scanning_state != ScanningState::Idle) {
        m_lib_internal->log(LogLevel::Info, [this](){ return "cancel scanning on device \"" + m_name + '"'; });
#ifndef SANE_PP_STUB
        if (fast)
            ::sane_cancel(m_handle);
#endif
        m_scanning_thread.request_stop();
    }
}

void device::check_for_scanning_error(std::unique_lock<std::mutex>&& lock) {
    std::exception_ptr ptr;

    {
        auto l = lock ? std::move(lock) : std::unique_lock{m_scanning_state_mutex};
        std::swap(ptr, m_last_scanning_error);
    }

    if ((m_scanning_state == ScanningState::Idle || ptr) && m_scanning_thread.joinable())
        m_scanning_thread.join();

    if (ptr)
        std::rethrow_exception(ptr);
}

const ::SANE_Parameters* device::get_scanning_parameters() {
    if (m_use_internal_waiter) {
        std::unique_lock lock{m_scanning_state_mutex};
        m_internal_state_waiting.wait(lock, [this](){
            return m_scanning_state == ScanningState::Scanning
                || m_scanning_state == ScanningState::Idle
                || m_last_scanning_error; });

        check_for_scanning_error(std::move(lock));
        return &m_scanning_params;
    }

    check_for_scanning_error(std::unique_lock{m_scanning_state_mutex});
    return (m_scanning_state == ScanningState::Scanning || m_scanning_state == ScanningState::Idle) ?
        &m_scanning_params : nullptr;
}

std::vector<unsigned char> device::get_scanning_data() {
    if (m_use_internal_waiter) {
        std::unique_lock lock{m_scanning_state_mutex};
        m_internal_state_waiting.wait(lock, [this](){
            return ! m_chunks.empty() || m_last_scanning_error; });

        check_for_scanning_error(std::move(lock));
    } else
        check_for_scanning_error(std::unique_lock{m_scanning_state_mutex});

    std::vector<unsigned char> res;

    {
        std::lock_guard guard{m_scanning_state_mutex};
        if (m_chunks.empty())
            throw std::logic_error("trying to get scanner data on \"" + m_name + "\" device "
                "while even parameters hasn't been got");
        res = std::move(m_chunks.front());
        m_chunks.pop_front();
    }

    if (res.empty() && m_scanning_thread.joinable())
        m_scanning_thread.join();

    return res;
}

void device::do_scanning(std::stop_token stop_token) {
    bool cancel_requested = false;

    m_lib_internal->log(LogLevel::Debug, [](){ return "background scanning started"; });

    try {
#ifdef SANE_PP_STUB
        using namespace std::chrono_literals;

        m_sample_image_offset = 0;

        m_scanning_state = ScanningState::Starting;
        m_scanning_state_notifier();

        std::this_thread::sleep_for(500ms);

        // Let's define trivial 32x32 monochrome image with black/white pixels
        m_scanning_params.format = SANE_FRAME_GRAY;
        m_scanning_params.last_frame = SANE_TRUE;
        m_scanning_params.bytes_per_line = 4;
        m_scanning_params.pixels_per_line = 32;
        m_scanning_params.lines = 32;
        m_scanning_params.depth = 1;

        m_scanning_state = ScanningState::Scanning;
        m_scanning_state_notifier();
#else
        m_scanning_state = ScanningState::Starting;
        m_scanning_state_notifier();

        details::checked_call("unable to start scanning", &::sane_start, m_handle);

        details::checked_call("unable to get scan parameters", &::sane_get_parameters,
            m_handle, &m_scanning_params);

        m_scanning_state = ScanningState::Scanning;
        m_scanning_state_notifier();
#endif

        m_lib_internal->log(LogLevel::Debug, [](){ return "parameters got, now start reading loop"; });

        bool run = true;
        std::size_t was_read_totally = 0;

        while (run) {
            const bool do_stop = stop_token.stop_requested();
            m_lib_internal->log(LogLevel::Debug, [do_stop](){
                return std::string{"check whether to stop -> "} + (do_stop ? "[true]" : "[false]"); });
            if (do_stop) {
#ifndef SANE_PP_STUB
                // Can block for a long time effectivey waiting until a device finishes its operation
                // instead of cancelling it. Pity.
                ::sane_cancel(m_handle);
#endif
                throw error_with_code("", SANE_STATUS_CANCELLED);
            }

            std::vector<unsigned char> chunk(4096*2);
            std::size_t was_read = 0;

            m_lib_internal->log(LogLevel::Debug,
                [&chunk, was_read_totally](){
                    return "going to read up to " + std::to_string(chunk.size())
                        + " bytes at offset " + std::to_string(was_read_totally); });

#ifdef SANE_PP_STUB
            auto was_read = std::min({chunk.size(), std::size(details::g_sample_image) - m_sample_image_offset, (size_t)11});
            std::copy(details::g_sample_image + m_sample_image_offset,
                details::g_sample_image + m_sample_image_offset + to_read, chunk.data());
            m_sample_image_offset += was_read;
            std::this_thread::sleep_for(500ms);
            chunk.resize(was_read);
            if (! chunk.empty()) {
                {
                    std::lock_guard guard{m_scanning_state_mutex};
                    m_chunks.push_back(std::move(chunk));
                }
                m_scanning_state_notifier();
            } else
                run = false;
#else
            ::SANE_Int len = 0;
            ::SANE_Status status = ::sane_read(m_handle, chunk.data(), chunk.size(), &len);

            if (status != SANE_STATUS_GOOD && status != SANE_STATUS_EOF)
                throw error_with_code("unable to read next packet of data from scanner", status);

            was_read = len;
            chunk.resize(was_read);
            if (! chunk.empty()) {
                {
                    std::lock_guard guard{m_scanning_state_mutex};
                    m_chunks.push_back(std::move(chunk));
                }
                m_scanning_state_notifier();
            }

            if (status == SANE_STATUS_EOF)
                run = false;
#endif
            was_read_totally += was_read;

            m_lib_internal->log(LogLevel::Debug,
                [was_read, was_read_totally](){
                    return "have read " + std::to_string(was_read) + " bytes at offset "
                        + std::to_string(was_read_totally); });
        }
    } catch (const error_with_code& er) {
        m_lib_internal->log(LogLevel::Debug, [&er](){
            return "got the wrapper exception with code " + std::to_string(er.get_code()); });
        if (er.get_code() == SANE_STATUS_CANCELLED)
            cancel_requested = true;
        else {
            std::lock_guard guard{m_scanning_state_mutex};
            m_last_scanning_error = std::current_exception();
        }
    } catch (...) {
        m_lib_internal->log(LogLevel::Debug, [](){ return "got an unknown exception"; });
        std::lock_guard guard{m_scanning_state_mutex};
        m_last_scanning_error = std::current_exception();
    }

#ifndef SANE_PP_STUB
    if (m_scanning_state == ScanningState::Scanning
        && ! cancel_requested
        && m_scanning_params.last_frame == SANE_TRUE)

        ::sane_cancel(m_handle);
#endif

    {
        std::lock_guard guard{m_scanning_state_mutex};
        m_chunks.push_back(std::vector<unsigned char>{});
    }

    m_scanning_state = ScanningState::Idle;
    m_scanning_state_notifier();

    m_lib_internal->log(LogLevel::Debug, [](){ return "background scanning finished"; });
}

} // ns vg_sane
