/**
 * @file      : memory_pool.hpp
 * @author    : LittleKu<kklvzl@gmail.com>
 * @date      : 2024-06-05 22:43:10
 * @brief     :
 */
#ifndef MEMORY_POOL_HPP_
#define MEMORY_POOL_HPP_

#include <atomic>
#include <cassert>
#include <mutex>

namespace memory_pool {

namespace internal {

template <typename T>
class HeapPool {
  using ThisType = HeapPool<T>;

  std::mutex mutex;
  unsigned char* pool = nullptr;
  ThisType* next = nullptr;

  std::atomic_ushort free = 0;
  std::atomic_ushort capacity = 0;

 public:
  explicit HeapPool(unsigned short c) : capacity(c) {
    assert(capacity > 0);
    init();
  }

  ~HeapPool() {
    if (next != nullptr) {
      delete next;
    }

    if (pool != nullptr) {
      delete[] pool;
    }
  }

  void* Allocate() {
    ThisType* pool = this;
    for (; pool != nullptr; pool = pool->next) {
      if (pool->free.fetch_sub(1) > 0) {
        break;
      }
      pool->free.fetch_add(1);
      {
        std::unique_lock<std::mutex> lock(pool->mutex);
        if (pool->next == nullptr) {
          pool->next = new ThisType(capacity);
        }
      }
    }
    return pool->AllocateBlock();
  }

  void Free(void* block) {
    ThisType* pool = GetBlockPool(block);
    pool->FreeBlock(block);
  }

 private:
  constexpr bool is_64bit() const { return sizeof(void*) == 8; }
  constexpr uint8_t block_header_size() const { return 4; }
  constexpr uint8_t pool_header_size() const { return 8; }
  void init() {
    assert(pool == nullptr);
    pool = new unsigned char
        [pool_header_size() /* store this object information */ +
         (block_header_size() /* store block information */ + sizeof(T)) *
             capacity];

    /* store this object information */
    size_t offset = 0;
    unsigned long long u_this = reinterpret_cast<unsigned long long>(this);
    if (is_64bit()) {
      pool[offset++] = (u_this >> 56) & 0xff;
      pool[offset++] = (u_this >> 48) & 0xff;
      pool[offset++] = (u_this >> 40) & 0xff;
      pool[offset++] = (u_this >> 32) & 0xff;
    } else {
      pool[offset++] = 0;
      pool[offset++] = 0;
      pool[offset++] = 0;
      pool[offset++] = 0;
    }
    pool[offset++] = (u_this >> 24) & 0xff;
    pool[offset++] = (u_this >> 16) & 0xff;
    pool[offset++] = (u_this >> 8) & 0xff;
    pool[offset++] = u_this & 0xff;

    /* store block information */
    for (uint16_t i = 0; i < capacity; ++i) {
      /* store block state [0/1]*/
      pool[offset++] = 0;

      /* reserve memory */
      pool[offset++] = 0;

      /* store block index */
      pool[offset++] = (i >> 8) & 0xff;
      pool[offset++] = i & 0xff;

      /* store block data */
      offset += sizeof(T);
    }
    free.store(capacity.load());
  }

  void* AllocateBlock() {
    std::unique_lock<std::mutex> lock(mutex);
    int pos = pool_header_size();
    unsigned char* block = nullptr;
    for (uint16_t i = 0; i < capacity; ++i) {
      if (pool[pos] == 0) {
        pool[pos] = 1; /* mark as used */
        block = &pool[pos + block_header_size()];
        break;
      }
      /* move to next block */
      pos = pos + sizeof(T) + block_header_size();
    }
    return block;
  }

  void FreeBlock(void* block) {
    unsigned char* block_header =
        reinterpret_cast<unsigned char*>(block) - block_header_size();
    {
      std::unique_lock<std::mutex> lock(mutex);
      block_header[0] = 0; /* mark as free */
    }
    free.fetch_add(1);
  }

  unsigned short GetBlockIndex(void* block) {
    unsigned char* block_header =
        reinterpret_cast<unsigned char*>(block) - block_header_size();
    return ((block_header[2] << 8) | block_header[3]);
  }

  ThisType* GetBlockPool(void* block) {
    unsigned short index = GetBlockIndex(block);
    unsigned char* current_pool = reinterpret_cast<unsigned char*>(block) -
                                  (index * (sizeof(T) + block_header_size())) -
                                  block_header_size() - pool_header_size();
    unsigned long long u_current_pool = 0;

    for (int i = 0; i < 8; ++i) {
      u_current_pool = (u_current_pool << 8) + current_pool[i];
    }
    return reinterpret_cast<ThisType*>(u_current_pool);
  }
};
}  // namespace internal

template <typename T,
          typename PoolAllocator = internal::HeapPool<T>,
          unsigned short min_count = 10>
class MemoryPool {
 public:
  using Ptr = T*;
  explicit MemoryPool(unsigned short count = min_count)
      : pool_allocator_(new PoolAllocator(count)) {}
  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;
  ~MemoryPool() {}

  template <typename... Args>
  auto Create(Args&&... args) {
    assert(pool_allocator_ != nullptr);
    auto* block = pool_allocator_->Allocate();
    return new (block) T(std::forward<Args>(args)...);
  }

  void Destroy(T* ptr) {
    assert(pool_allocator_ != nullptr);
    ptr->~T();
    pool_allocator_->Free(ptr);
  }

 private:
  PoolAllocator* pool_allocator_ = nullptr;
};

}  // namespace memory_pool

#endif  // MEMORY_POOL_HPP_