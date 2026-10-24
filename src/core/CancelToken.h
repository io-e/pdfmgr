#pragma once
#include <memory>

namespace pdfcore {
// Cooperative cancellation backed by a single MuPDF fz_cookie. cancel() sets
// cookie.abort, which MuPDF polls while interpreting the display list, so no
// watcher thread is needed. Cheap to copy (shared state).
class CancelToken {
public:
    CancelToken();
    void cancel() const;
    bool isCancelled() const;
    // The underlying fz_cookie* (opaque here: fz_cookie is an anonymous struct
    // typedef and cannot be forward-declared). Cast in MuPDF-aware code only.
    void* nativeCookie() const;
private:
    struct Box;
    std::shared_ptr<Box> m_box;
};
}
