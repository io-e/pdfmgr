#include "CancelToken.h"
#include <mupdf/fitz.h>
#include <atomic>

namespace pdfcore {
struct CancelToken::Box { fz_cookie cookie{}; };
CancelToken::CancelToken() : m_box(std::make_shared<Box>()) {}
void* CancelToken::nativeCookie() const { return &m_box->cookie; }
// MuPDF documents fz_cookie.abort as set by another thread and polled by the
// renderer; atomic_ref keeps our side of that free of data races.
void CancelToken::cancel() const { std::atomic_ref<int>(m_box->cookie.abort).store(1, std::memory_order_relaxed); }
bool CancelToken::isCancelled() const { return std::atomic_ref<int>(m_box->cookie.abort).load(std::memory_order_relaxed) != 0; }
}
