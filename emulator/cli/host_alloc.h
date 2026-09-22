#pragma once
#include "pedal/core.h"
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

// Heap-backed arena for the host; the Daisy wrapper replaces this with SDRAM.
class HostAllocator final : public pedal::Allocator {
public:
    ~HostAllocator() override {
#ifdef _WIN32
        for (void* p : blocks_) _aligned_free(p);
#else
        for (void* p : blocks_) std::free(p);
#endif
    }
    void* alloc(size_t bytes, size_t align) override {
        if (align < sizeof(void*)) align = sizeof(void*);
#ifdef _WIN32
        void* p = _aligned_malloc(bytes, align);
        if (!p) return nullptr;
#else
        void* p = nullptr;
        if (posix_memalign(&p, align, bytes) != 0) return nullptr;
#endif
        blocks_.push_back(p); total_ += bytes; return p;
    }
    size_t totalBytes() const { return total_; }
private:
    std::vector<void*> blocks_; size_t total_ = 0;
};
