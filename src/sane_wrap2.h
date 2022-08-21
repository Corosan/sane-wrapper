#pragma once

/** @mainpage Sane C++ Wrapper
 *
 * Object-oriented wrapper around Sane C library for accessing to scanners
 * (https://sane-project.gitlab.io/standard/).
 *
 * Design controversy: while developing a library wrapper offering a set of dependent entities, we
 * can select desired point on a path between two points: a) trying to make all objects valid all
 * the time from C++ perspective but signal via errors/exceptions mechanism about their
 * inappropriate state; b) not trying this but just having long descriptive help around which would
 * say when an entity becomes invalid and further usage of it yields to undefined behavior (crash in
 * most cases). It what's C++ standard offers in most cases in STL.
 *
 * (a) approach requires more careful and more complicated design because it covers (b) by some
 * wrapper objects which check internal invariants and save external callers from pieces of code
 * which could yield to undefined behavior. But to be honest in both cases an external caller must
 * know what he/she doing because getting cryptic exceptions not seriously better than getting a
 * simple crash. So let's try to select something in the middle of.
 *
 * I. Working with library singleton
 * ---------------------------------
 *
 * ~~~~~~~~{.cpp}
 * i_lib* lib_ptr = lib::instance(); // first call initializes library if it's not
 *                                   // initialized before or have being closed
 * ...
 * // next call invalidates all objects got from the library
 * lib_ptr->close(); // or lib::close();
 * ~~~~~~~~
 *
 * Motivation: a library is a singleton, we can't open a few instances of it. On the other hand, we
 * don't want to close the library and invalidate all dependent objects after any entity got from
 * the library (for example, lib_ptr) goes out of scope.  That's why the library is represented by
 * a raw pointer here. We should be able to call the library just by "lib::instance()" expression
 * in whatever place we want.
 *
 * II. Working with device descriptions
 * ------------------------------------
 *
 * Every scanner device before being opened should be discovered. The library offers a enumeration
 * of device descriptions for this. A description from C library perspective is a pointer to
 * ::SANE_Device struct carrying a few fields. No worth in copying them for trivial enumeration
 * process. But the pointer becomes invalid on next call of C API. Either we would wrap the pointer
 * with a true value-type object (thus introducing redundant copying on some stage of walking the
 * object through the code) or provide it separately as a helper accessible for end user.
 *
 * I keep both approaches so far, thus, working with device descriptions:
 *
 * ~~~~~~~~{.cpp}
 * // get_device_descrs() returns a range of light-weight objects - descriptions
 * // which are valid until next call to get_device_descrs() is made. A [range] doesn't
 * // own any of them, it's just a view on some internal state hold by the library.
 * for (const device_descr& d: lib_ptr->get_device_descrs()) {
 *     std::cout << "name: " << d.name() << "; vendor: " << d.vendor() << std::endl;
 *     ...
 * }
 * ~~~~~~~~
 *
 * Motivation: internally device_descr initially wraps a pointer to ::SANE_Device structure. The
 * pointer becomes invalidated after next call to get_device_descrs(). But from a user perspective
 * it would be natural to work with device description as with true value type object - it's just a
 * number of fields eventually. Thus the object while being copied - copies all the data from
 * underlying pointer into internal fields for further keeping.
 *
 * ~~~~~~~~{.cpp}
 * device_descr descr; // empty description with empty (but valid) fields
 * for (const device_descr& d : lib_ptr->get_device_descrs()) {
 *     if (d.vendor() == "HP")
 *         descr = d; // all the data is copied from ::SANE_Device fields into internal storage
 * }
 *
 * std::cout << descr.name() << std::endl; // could be used even after next call to
 *                                         // get_device_descrs()
 * ~~~~~~~~
 *
 * Copying could be optimized to copy-on-change semantic later because we know the point of time
 * when underlying pointer becomes invalid. But this will require to keep the list of all
 * device_descr objects from inside the library wrapper to run copying step on before processing
 * get_device_list() call. Not sure it worth it and it doesn't change external behavior.
 *
 * One more point to keep in mind - devices could be changed externally - a user of the system can
 * plug/unplug physical scanner at any time. Is it worth to think so much on providing type-value
 * wrappers for a piece of information which theoretically could change due to external event?
 *
 * III. Working with devices
 * -------------------------
 *
 * Working with devices. Device object represents actual scanner device. It's reasonable to keep the
 * object while the scanner is needed and to close the scanner API when the object goes out of
 * scope. Not copying allowed because it's meaningless - it doesn't create new real scaller in
 * material world. Though Sane library could allow to open the same device more than once, it's
 * strange and meaningless. But as long a user in a real world could unplug one scanner and plug
 * another one with the same name, it's not clear what rule to enforce here. The simplest check
 * could be - only one device with specific name could exist at one point of time.
 *
 * ~~~~~~~~{.cpp}
 * // opens a device specified by a name. It's valid until the object d goes out of scope.
 * // Usage scenario is close to using of unique_ptrs.
 * device dev1 = lib_ptr->open_device("...");
 * // device dev2 = dev1; // - prohibited
 * device dev2 = std::move(dev1);  // allowed, d is invalid after this (undefined behavior if
 *                                 // accesing it)
 * device dev3;  // also invalid state.
 * ~~~~~~~~
 *
 * IV. Working with device options
 * -------------------------------
 *
 * And options are coming next. What is an option? Some typed property of a device which has
 * name, description, optionally range of possible values. It could be very simple entity if not the
 * fact that a set of options can change or some option values can change after setting another
 * option's value. C style interface looks pretty simple - get_option(...) and set_option(...)
 * methods in a nutshell. But what's typical usage? To display all the options via some GUI
 * possibly. How to implement responsive GUI? We need to know what's exactly changed after setting
 * some option's value. Either it will be implemented via complicated logic in GUI code or... it
 * must be implemented in the library wrapper.
 *
 * As long as the underlying library doesn't offer facilities for tracking what exactly changed in
 * the options set, the wrapper library must fully reload all options and use sophisticated logic
 * for deciding, which options added/removed/changed. Let's stand on a simple approach instead so
 * far: the wrapper library allow to iterate over option descripions only. If particular changing
 * operation signals that something could change, it's a user responsibility to refresh their state
 * by repeated reading of options' descriptions and values.
 *
 * ~~~~~~~~{.cpp}
 * int idx_br = {};
 * for (auto& [idx, descriptor] : dev.get_options()) {
 *     std::cout << idx << ": " << descriptor->name << std::endl;
 *     if (descriptor->name == std::string{"brightness"})
 *         idx_br = idx;
 * }
 * ...
 * dev.set_option(idx_br, 100); // set brightness, potentially can change everything
 * ~~~~~~~~
 *
 * As long as an option has a type of its value (boolean, integer, string...), it operates with
 * std::variant types for setting/getting it.
 *
 */

#include "sane_wrap2_device_descr.h"

#include <cstdint>
#include <utility>
#include <memory>
#include <ranges>

#include <sane/sane.h>

inline constexpr bool operator==(const ::SANE_Device** devices, std::default_sentinel_t) {
    return ! *devices;
}

namespace vg_sane {

class lib;

class device final {
public:
    class option_iterator final {
    public:
        using difference_type = std::intptr_t;
        using value_type = std::pair<int, const ::SANE_Option_Descriptor*>;
        using reference_type = value_type;
        using pointer_type = const value_type*;
        using iterator_category = std::random_access_iterator_tag;

        option_iterator() {}

        reference_type operator*() const { return operator[](0); }
        reference_type operator[](int) const;
        pointer_type operator->() const;

        option_iterator& operator++() { ++m_pos; return *this; }
        option_iterator operator++(int) { auto t = *this; ++m_pos; return t; }
        option_iterator& operator--() { --m_pos; return *this; }
        option_iterator operator--(int) { auto t = *this; --m_pos; return t; }

        option_iterator& operator+=(int v) { m_pos += v; return *this; }
        option_iterator& operator-=(int v) { m_pos -= v; return *this; }

        difference_type operator-(const option_iterator& r) const {
            return m_pos - r.m_pos;
        }

        bool operator==(const option_iterator&) const = default;
        auto operator<=>(const option_iterator&) const = default;

        friend option_iterator operator+(const option_iterator& i, int v) {
            return {i.m_parent, i.m_pos + v};
        }
        friend option_iterator operator+(int v, const option_iterator& i) {
            return {i.m_parent, i.m_pos + v};
        }
        friend option_iterator operator-(const option_iterator& i, int v) {
            return {i.m_parent, i.m_pos - v};
        }
        friend option_iterator operator-(int v, const option_iterator& i) {
            return {i.m_parent, v - i.m_pos};
        }

    private:
        friend device;

        const device* m_parent = {};
        int m_pos = 1;

        option_iterator(const device* parent, int pos)
            : m_parent{parent}, m_pos{pos} {
        }
    };

    device() = default;
    device(const device&& r)
        : m_handle{r.m_handle}, m_name{std::move(r.m_name)} {
    }
    device& operator=(const device&& r) {
        auto t = std::move(r);
        swap(t);
        return *this;
    }
    ~device() {
        if (m_handle)
            ::sane_close(m_handle);
    }

    void swap(device& r);

    std::ranges::subrange<option_iterator>
    get_option_descrs() const;

private:
    friend lib;

    ::SANE_Handle m_handle = {};
    std::string m_name;

    device(::SANE_Handle h, std::string name)
        : m_handle(h), m_name(std::move(name)) {}
};

inline void swap(device& l, device& r) { l.swap(r); }

class lib final {
public:
    static lib* instance();

    std::ranges::subrange<device_descr_iterator, std::default_sentinel_t>
    get_device_descrs(bool reload = false);

    std::ranges::subrange<const ::SANE_Device**, std::default_sentinel_t>
    get_device_descrs_raw(bool reload = false);

    device open_device(const char* name);

private:
    ::SANE_Int m_sane_ver{};
    const ::SANE_Device **m_sane_devices = {};

    lib();
    ~lib();
};

static_assert(std::random_access_iterator<device::option_iterator>);

} // ns vg_sane
