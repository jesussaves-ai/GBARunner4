#pragma once
#include <cstddef>

// Replaceable operator new is not noexcept per the standard (only operator
// delete is), and GCC's libstdc++ headers declare it that way too.
void* operator new(std::size_t blocksize);
void* operator new(std::size_t size, std::align_val_t al);
void* operator new[](std::size_t blocksize);
void* operator new[](std::size_t size, std::align_val_t al);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, std::align_val_t align) noexcept;
void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, std::align_val_t align) noexcept;
void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept;

constexpr std::align_val_t cache_align { 32 };

void heap_init();
