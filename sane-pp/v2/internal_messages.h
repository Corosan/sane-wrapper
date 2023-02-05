#pragma once

#include <utility>
#include <memory>
#include <variant>
#include <tuple>
#include <exception>

namespace vg_sane {

class device_enumerator;

namespace messages {

struct enumerate_args {};
struct enumerate_result {};

using enumerate_call_event_t = std::tuple<
    std::weak_ptr<device_enumerator>,
    void (device_enumerator::*)(std::variant<enumerate_result, std::exception_ptr>),
    enumerate_args>;

} // ns messages

using fwd_messages_t = std::variant<messages::enumerate_call_event_t>;

} // ns vg_sane

