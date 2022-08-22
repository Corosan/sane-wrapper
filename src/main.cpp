#include <iostream>
#include <stdexcept>

// #include "sane_wrap.h"
// #include "sane_wrap2.h"
#include "v3/sane_wrapper.h"

// const int& f();
// template<class T> struct D;
// D<decltype(f())> d;

int main() {
/*
    try {
        auto sane = sg_sane::lib::instance();

        for (auto d : sane->get_device_infos()) {
            std::cout << d.name() << std::endl;
        }

        for (auto p : sane->get_device_infos_raw()) {
            std::cout << p->name << std::endl;
        }

        auto d = sane->open_device("abc");
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << std::endl;
    }
*/
/*
    try {
        auto* sane = vg_sane::lib::instance();
        std::string first_name;

        for (const auto& d : sane->get_device_descrs()) {
            std::cout << d.name() << std::endl;
            if (first_name.empty())
                first_name = d.name();
        }

        for (auto p : sane->get_device_descrs_raw()) {
            std::cout << p->name << std::endl;
        }

        auto d = sane->open_device(first_name.c_str());
        for (auto [idx, ptr] : d.get_option_descrs()) {
            std::cout << "  " << idx << ": " << ptr->name << "; "
                << (ptr->title ? ptr->title : "") << "; " << (ptr->desc ? ptr->desc : "") << std::endl;
        }
*/
    try {
        auto sane_lib = vg_sane::lib::instance();

        std::string some_name;
        for (auto p : sane_lib->get_device_infos()) {
            std::cout
                << "device : " << (p->name ? p->name : "") << '\n'
                << "vendor : " << (p->vendor ? p->vendor : "") << '\n'
                << "model  : " << (p->model ? p->model : "") << '\n'
                << "type   : " << (p->type ? p->type : "") << "\n--------"
                << std::endl;
            if (some_name.empty())
                some_name = p->name;
        }

        if (! some_name.empty()) {
            std::cout << "trying to open device \"" + some_name + "\" ..." << std::endl;
            auto dev = sane_lib->open_device(some_name.c_str());

            std::cout << "options:\n";
            for (auto p : dev.get_option_infos()) {
                std::cout
                    << "  [" << p->name << "]\n"
                    << "    title: " << (p->title ? p->title : "") << '\n'
                    << "    desc : " << (p->desc ? p->desc : "") << '\n'
                    << "    type : " << p->type
                    << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << std::endl;
    }
}
