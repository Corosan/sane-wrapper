// vi: textwidth=100
#pragma once

#include "internal_messages.h"

#include <memory>
#include <exception>
#include <functional>

/*
 Notes about objects lifetime

 1. The wrapper representing global library object is a singleton - it's enforced by its design.
Though it can be created and destroyed more than once during a program life. The wrapper should be
unlocked and thus destroyed before the main() function scope closes. Better to unlock it at the end
of the scope where it has been created.

 2. The wrapper offers instance() static function to returning locking pointer to the singleton and
to create the global library object if no one before. Another method weak_instance() returns just a
weak pointer without any creation activity - it's used by various slave objects when asynchronous
operation is requested.

 3. Slave objects don't lock the global library object - they just wouldn't work if it's released /
not created. Asynchronous operations don't lock slave objects from being destroyed, no undefined
behavior should be exposed by this. But the operations should be aborted as fast as possible.
 */

namespace vg_sane {

class device_enumerator;

class lib final {
public:
    static std::shared_ptr<lib> instance();
    static std::weak_ptr<lib> weak_instance() {
        return m_instance;
    }

    lib(const lib&) = delete;
    lib& operator=(const lib&) = delete;

    void set_worker_notifier(std::function<void()> val);
    void set_api_notifier(std::function<void()> val);

    void worker_dispatch();
    void api_dispatch();

    ~lib();

private:
    friend device_enumerator;

    class impl;

    static std::weak_ptr<lib> m_instance;

    std::unique_ptr<impl> m_impl;

    lib();
};

struct device_enumerator_events {
    virtual ~device_enumerator_events() = default;
    virtual void enabled(bool val) = 0;
    virtual void list_changed() = 0;
    virtual void unhandled_exception(std::exception_ptr) = 0;
};

class device_enumerator : public std::enable_shared_from_this<device_enumerator> {
public:
    static std::shared_ptr<device_enumerator> create() {
        return std::shared_ptr<device_enumerator>{new device_enumerator};
    }

    void start_enumerate();

    void set_events_receiver(device_enumerator_events* val) {
        m_events_receiver = val;
    }

private:
    device_enumerator_events* m_events_receiver = nullptr;
    bool m_enabled = true;

    device_enumerator() = default;

    void handle_result(std::variant<messages::enumerate_result, std::exception_ptr> val);
};

} // ns vg_sane
