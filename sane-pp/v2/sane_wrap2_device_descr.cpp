#include "sane_wrap2_device_descr.h"

namespace vg_sane {

device_descr::device_descr(const device_descr& r)
    : m_name{r.name()}
    , m_vendor{r.vendor()}
    , m_model{r.model()}
    , m_type{r.type()} {
}

// Design choice: should we propagate internal pointer for eliminating copying of text fields while
// device_descr is moved by user code? It could be more efficient, but can lead to dangling pointers
// if a user will want to store an object by assigning value by r-val: m_descr = std::move(descr);
// Let's select less efficient but more safe way.
device_descr::device_descr(device_descr&& r)
    : m_name{r.m_sane_ptr ? std::string{m_sane_ptr->name} : std::move(r.m_name)}
    , m_vendor{r.m_sane_ptr ? std::string{m_sane_ptr->vendor} : std::move(r.m_vendor)}
    , m_model{r.m_sane_ptr ? std::string{m_sane_ptr->model} : std::move(r.m_model)}
    , m_type{r.m_sane_ptr ? std::string{m_sane_ptr->type} : std::move(r.m_type)} {
}

void device_descr::swap(device_descr& r) {
    using std::swap;
    swap(m_sane_ptr, r.m_sane_ptr);
    swap(m_name, r.m_name);
    swap(m_vendor, r.m_vendor);
    swap(m_model, r.m_model);
    swap(m_type, r.m_type);
}

} // ns vg_sane
