#include "MuPdfContext.h"
#include <mupdf/fitz.h>
#include <stdexcept>

namespace pdfcore {
static_assert(FZ_LOCK_MAX <= 32);

void MuPdfContext::lock(void* user, int l) { static_cast<MuPdfContext*>(user)->m_mutexes[l].lock(); }
void MuPdfContext::unlock(void* user, int l) { static_cast<MuPdfContext*>(user)->m_mutexes[l].unlock(); }

MuPdfContext::MuPdfContext() {
    fz_locks_context locks{this, &MuPdfContext::lock, &MuPdfContext::unlock};
    m_ctx = fz_new_context(nullptr, &locks, FZ_STORE_DEFAULT);
    if (!m_ctx) throw std::runtime_error("fz_new_context failed");
    fz_try(m_ctx) { fz_register_document_handlers(m_ctx); }
    fz_catch(m_ctx) { throw std::runtime_error("fz_register_document_handlers failed"); }
}
MuPdfContext::~MuPdfContext() { if (m_ctx) fz_drop_context(m_ctx); }
MuPdfContext& MuPdfContext::instance() { static MuPdfContext inst; return inst; }
fz_context* MuPdfContext::clone() const { return fz_clone_context(m_ctx); }

ScopedClone::ScopedClone() : m_ctx(MuPdfContext::instance().clone()) {
    if (!m_ctx) throw std::runtime_error("fz_clone_context failed");
}
ScopedClone::~ScopedClone() { fz_drop_context(m_ctx); }
}
