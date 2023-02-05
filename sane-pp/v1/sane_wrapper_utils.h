#pragma once

#include <utility>
#include <stdexcept>
#include <string>
#include <functional>

#include <sane/sane.h>

namespace vg_sane {

class error : public std::runtime_error {
public:
    using runtime_error::runtime_error;
};

class error_with_code : public error {
    static std::string get_msg(std::string msg_prefix, ::SANE_Status code) {
        auto msg = ::sane_strstatus(code);
        if (msg_prefix.empty())
            return msg;
        else {
            msg_prefix.append(": ");
            msg_prefix.append(msg);
            return msg_prefix;
        }
    }

    ::SANE_Status m_code;
public:
    explicit error_with_code(std::string msg_prefix, ::SANE_Status code)
        : error(get_msg(std::move(msg_prefix), code)), m_code{code} {}

    ::SANE_Status get_code() const { return m_code; }
};

namespace details {

template <class Msg, class ... Parms, class ... Args>
void checked_call(Msg&& msg, ::SANE_Status (*f)(Parms ...), Args&& ... args) {
    if (auto status = (*f)(std::forward<Args>(args) ...); status != SANE_STATUS_GOOD) {
        if constexpr (std::is_convertible_v<Msg, std::string>)
            throw error_with_code(std::forward<Msg>(msg), status);
        else if constexpr (std::is_invocable_v<Msg>)
            throw error_with_code(std::invoke(std::forward<Msg>(msg)), status);
        else
            static_assert(!sizeof(Msg));
    }
}

} // ns details
} // ns vg_sane
