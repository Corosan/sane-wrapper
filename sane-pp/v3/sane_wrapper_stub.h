#pragma once

#include <cstring>
#include <string_view>
#include <thread>
#include <chrono>
#include <initializer_list>
#include <algorithm>

#include <sane/sane.h>

namespace vg_sane::details {

struct stub_option {
    ::SANE_Option_Descriptor m_d;
    std::string m_name;
    std::string m_title;
    std::string m_descr;
    std::vector<char> m_data;
    std::vector<std::string> m_str_constraint;
    std::vector<const char*> m_str_raw_constraint;
    std::vector<::SANE_Word> m_int_list_constraint;
    ::SANE_Range m_int_range;

    stub_option(std::string name, std::string title, std::string descr, ::SANE_Value_Type type, ::SANE_Int cap, std::size_t size = 1)
        : m_d{}
        , m_name(std::move(name))
        , m_title(std::move(title))
        , m_descr(std::move(descr)) {
        m_d.name = m_name.c_str();
        m_d.title = m_title.c_str();
        m_d.desc = m_descr.c_str();
        m_d.type = type;
        m_d.cap = cap;
        switch (type) {
        case ::SANE_TYPE_BOOL: m_d.size = sizeof(::SANE_Word); break;
        case ::SANE_TYPE_INT:
        case ::SANE_TYPE_FIXED: m_d.size = sizeof(::SANE_Word) * size; break;
        case ::SANE_TYPE_STRING: m_d.size = size; break;
        }
        m_data.resize(m_d.size);
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

    stub_option& set_int_range_constraint(const ::SANE_Range& range) {
        m_int_range = range;
        m_d.constraint_type = SANE_CONSTRAINT_RANGE;
        m_d.constraint.range = &m_int_range;
        return *this;
    }

    stub_option& set_int_list_constraint(std::vector<int> nums) {
        m_int_list_constraint.clear();
        m_int_list_constraint.push_back(static_cast<::SANE_Word>(nums.size()));
        for (int n : nums)
            m_int_list_constraint.push_back(static_cast<::SANE_Word>(n));
        m_d.constraint_type = SANE_CONSTRAINT_WORD_LIST;
        m_d.constraint.word_list = m_int_list_constraint.data();
        return *this;
    }

    template <typename T>
    [[nodiscard]] T* data() { return reinterpret_cast<T*>(m_data.data()); }

    template <typename T>
    [[nodiscard]] T& value(int pos = 0) { return *(reinterpret_cast<T*>(m_data.data()) + pos); }

    template <typename T> struct val_setter {
        stub_option& m_parent;

        template <typename A>
        val_setter& operator=(std::initializer_list<A> vals) {
            std::copy(std::begin(vals), std::end(vals), m_parent.data<T>());
            return *this;
        }
    };

    template <typename T>
    [[nodiscard]] auto values() { return val_setter<T>{*this}; }

    template <typename P> struct str_accessor {
        P& m_parent;

        str_accessor(P& parent) : m_parent{parent} {}

        str_accessor& operator=(std::string_view s) {
            auto len = std::min<std::size_t>(s.size(), m_parent.m_d.size - 1);
            m_parent.m_data.assign(std::begin(s), std::begin(s) + len);
            m_parent.m_data.push_back('\0');
            return *this;
        }

        operator std::string_view() const {
            return m_parent.m_data.data();
        }
    };

    [[nodiscard]] auto str() { return str_accessor{*this}; }
    [[nodiscard]] auto str() const { return str_accessor{*this}; }
};

}

