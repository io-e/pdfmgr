#pragma once
#include <memory>
#include <mutex>
#include <array>
struct fz_context;

namespace pdfcore {
// Owns the base fz_context with lock callbacks. Workers use clone().
class MuPdfContext {
public:
    static MuPdfContext& instance();
    fz_context* base() const { return m_ctx; }
    // Returns a cloned context (caller must fz_drop_context, or use ScopedClone).
    fz_context* clone() const;
    ~MuPdfContext();
    MuPdfContext(const MuPdfContext&) = delete;
    MuPdfContext& operator=(const MuPdfContext&) = delete;
private:
    MuPdfContext();
    fz_context* m_ctx = nullptr;
    std::array<std::mutex, 32> m_mutexes; // >= FZ_LOCK_MAX
    static void lock(void* user, int lock);
    static void unlock(void* user, int lock);
};

class ScopedClone {
public:
    ScopedClone();
    ~ScopedClone();
    fz_context* get() const { return m_ctx; }
    operator fz_context*() const { return m_ctx; }
private:
    fz_context* m_ctx;
};
}
