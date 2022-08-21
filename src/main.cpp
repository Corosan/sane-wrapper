#include <iostream>
#include <stdexcept>

// #include "sane_wrap.h"
#include "sane_wrap2.h"

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

    try {
        auto* sane = vg_sane::lib::instance();
        std::string first_name;

        for (const auto& d : sane->get_device_descrs()) {
            std::cout << d.name() << std::endl;
            if (first_name.empty())
                first_name = d.name();
        }
/*
        for (auto p : sane->get_device_descrs_raw()) {
            std::cout << p->name << std::endl;
        }
*/
        auto d = sane->open_device(first_name.c_str());
        for (auto [idx, ptr] : d.get_option_descrs()) {
            std::cout << "  " << idx << ": " << ptr->name << "; "
                << (ptr->title ? ptr->title : "") << "; " << (ptr->desc ? ptr->desc : "") << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << std::endl;
    }
}
