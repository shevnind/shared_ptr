#include <iostream>
#include <memory>

namespace shared_and_weak_ptr_block {
    struct BaseBlock {
        size_t shared_cnt;
        size_t weak_cnt;

        BaseBlock(size_t shared_cnt, size_t weak_cnt) : shared_cnt(shared_cnt), weak_cnt(weak_cnt) {}

        virtual void detach_shared_ptr(void* ptr) = 0;

        virtual void detach_weak_ptr() = 0;

        virtual ~BaseBlock() {}
    };

    template <typename U>
    struct BlockMakeShared final: BaseBlock {
        U value;

        template <typename... Args>
        BlockMakeShared(Args&&... args) : BaseBlock(1, 0), value(std::forward<Args>(args)...) {}

        void detach_shared_ptr([[maybe_unused]] void* ptr) override {
            --shared_cnt;
            if (shared_cnt == 0) {
                if (weak_cnt == 0) {
                    this->~BlockMakeShared();
                    ::operator delete(this);
                } else {
                    value.~U();
                }
            }
        }

        void detach_weak_ptr() override {
            --weak_cnt;
            if (shared_cnt == 0 && weak_cnt == 0) {
                ::operator delete(this);
            }
        }
    };


    template <typename U, typename Alloc>
    struct BlockAllocateShared final : BaseBlock {
        using BlockAlloc = typename std::allocator_traits<Alloc>:: template rebind_alloc<BlockAllocateShared>;

        U value;
        [[no_unique_address]] BlockAlloc alloc;

        template <typename... Args>
        BlockAllocateShared(Alloc alloc, Args&&... args) : BaseBlock(1, 0),
                                                           value(std::forward<Args>(args)...), alloc(alloc) {}

        void detach_shared_ptr([[maybe_unused]] void* ptr) override {
            --shared_cnt;
            if (shared_cnt == 0) {
                if (weak_cnt == 0) {
                    std::allocator_traits<BlockAlloc>::destroy(alloc, this);
                    std::allocator_traits<BlockAlloc>::deallocate(alloc, this, 1);
                } else {
                    std::allocator_traits<BlockAlloc>::destroy(alloc, &value);
                }
            }
        }

        void detach_weak_ptr() override {
            --weak_cnt;
            if (shared_cnt == 0 && weak_cnt == 0) {
                std::allocator_traits<BlockAlloc>::deallocate(alloc, this, 1);
            }
        }
    };


    template <typename U, typename Deleter = std::default_delete<U>, typename Alloc = std::allocator<U>>
    struct Block final : BaseBlock {
        using type_name_ptr = U*;
        using BlockAlloc = typename std::allocator_traits<Alloc>:: template rebind_alloc<Block<U, Deleter, Alloc>>;

        [[no_unique_address]] Deleter deleter;
        [[no_unique_address]] BlockAlloc alloc;

        Block(size_t shared_cnt, size_t weak_cnt) : BaseBlock(shared_cnt, weak_cnt) {};

        Block(BlockAlloc alloc) : BaseBlock(1, 0), deleter(), alloc(alloc) {}

        Block(Deleter deleter, BlockAlloc alloc) : BaseBlock(1, 0), deleter(deleter), alloc(alloc) {}

        void detach_shared_ptr(void* ptr) override {
            --shared_cnt;
            if (shared_cnt == 0) {
                if (weak_cnt == 0) {
                    deleter(static_cast<type_name_ptr>(ptr));
                    this->~Block();
                    std::allocator_traits<BlockAlloc>::deallocate(alloc, this, 1);
                } else {
                    deleter(static_cast<type_name_ptr>(ptr));
                }
            }
        }

        void detach_weak_ptr() override {
            --weak_cnt;
            if (shared_cnt == 0 && weak_cnt == 0) {
                this->~Block();
                std::allocator_traits<BlockAlloc>::deallocate(alloc, this, 1);
            }
        }
    };
} // namespace shared_and_weak_ptr_block

template <typename T>
class SharedPtr {
    template <typename U, typename... Args>
    friend SharedPtr<U> makeShared(Args&&...);

    template <typename U, typename Alloc, typename... Args>
    friend SharedPtr<U> allocateShared(const Alloc&, Args&&...);

    template <typename U>
    friend class WeakPtr;

    using BaseBlock = shared_and_weak_ptr_block::BaseBlock;
    template <typename U>
    using BlockMakeShared = shared_and_weak_ptr_block::BlockMakeShared<U>;
    template <typename U, typename Alloc>
    using BlockAllocateShared = shared_and_weak_ptr_block::BlockAllocateShared<U, Alloc>;
    template <typename U, typename Deleter = std::default_delete<U>, typename Alloc = std::allocator<U>>
    using Block = shared_and_weak_ptr_block::Block<U, Deleter, Alloc>;


    T* ptr;
    BaseBlock* block;

    template <typename U>
    SharedPtr(BlockMakeShared<U>* new_block) : ptr(&new_block->value), block(new_block) {
        check_enable_shared();
    }

    template <typename U, typename Alloc = std::allocator<U>>
    SharedPtr(BlockAllocateShared<U, Alloc>* new_block) : ptr(&new_block->value), block(new_block) {
        check_enable_shared();
    }

    SharedPtr(T* ptr, BaseBlock* block) : ptr(ptr), block(block) {
        if (block != nullptr) ++block->shared_cnt;
        check_enable_shared();
    }

    void detach() {
        if (block != nullptr) block->detach_shared_ptr(ptr);
        clear();
    }

    void check_enable_shared();

public:
    SharedPtr(): ptr(nullptr), block(nullptr) {}

    template <typename U>
    SharedPtr(U* ptr): ptr(ptr) {
        using BlockAlloc = typename std::allocator_traits<std::allocator<T>>:: template rebind_alloc<Block<U>>;
        BlockAlloc alloc;
        block = std::allocator_traits<BlockAlloc>::allocate(alloc, 1);
        new(block) Block<U>(alloc);
        check_enable_shared();
    }

    template <typename U, typename Deleter>
    SharedPtr(U* ptr, Deleter deleter) : ptr(ptr) {
        using BlockAlloc = typename std::allocator_traits<std::allocator<T>>:: template rebind_alloc<Block<U, Deleter>>;
        BlockAlloc alloc;
        block = std::allocator_traits<BlockAlloc>::allocate(alloc, 1);
        new(block) Block<U, Deleter>(deleter, alloc);
        check_enable_shared();
    }

    template <typename U, typename Deleter, typename Alloc>
    SharedPtr(U* ptr, Deleter deleter, Alloc old_alloc) : ptr(ptr) {
        using BlockAlloc = typename std::allocator_traits<Alloc>:: template rebind_alloc<Block<U, Deleter, Alloc>>;
        BlockAlloc alloc(old_alloc);
        block = std::allocator_traits<BlockAlloc>::allocate(alloc, 1);
        new(block) Block<U, Deleter, Alloc>(deleter, alloc);
        check_enable_shared();
    }

    template <typename U, typename R>
    SharedPtr(const SharedPtr<U>& other, R* new_ptr): ptr(new_ptr),
                                                      block(static_cast<BaseBlock*>(other.get_block_ptr())) {
        if (block != nullptr) ++block->shared_cnt;
        check_enable_shared();
    }

    SharedPtr(const SharedPtr<T>& other) : ptr(other.get()), block(static_cast<BaseBlock*>(other.get_block_ptr())) {
        if (block != nullptr) ++block->shared_cnt;
        check_enable_shared();
    }

    template <typename U>
    SharedPtr(const SharedPtr<U>& other) : ptr(other.get()), block(static_cast<BaseBlock*>(other.get_block_ptr())) {
        if (block != nullptr) ++block->shared_cnt;
        check_enable_shared();
    }

    SharedPtr(SharedPtr<T>&& other) noexcept : ptr(other.get()), block(static_cast<BaseBlock*>(other.get_block_ptr())) {
        other.clear();
    }

    template <typename U>
    SharedPtr(SharedPtr<U>&& other) noexcept : ptr(other.get()), block(static_cast<BaseBlock*>(other.get_block_ptr())) {
        other.clear();
    }

    SharedPtr& operator=(const SharedPtr<T>& other) {
        detach();
        ptr = other.get();
        block = static_cast<BaseBlock*>(other.get_block_ptr());
        if (block != nullptr) ++block->shared_cnt;
        return *this;
    }

    template <typename U>
    SharedPtr& operator=(const SharedPtr<U>& other) {
        detach();
        ptr = other.get();
        block = static_cast<BaseBlock*>(other.get_block_ptr());
        if (block != nullptr) ++block->shared_cnt;
        return *this;
    }

    SharedPtr& operator=(SharedPtr<T>&& other) noexcept {
        detach();
        ptr = other.get();
        block = static_cast<BaseBlock*>(other.get_block_ptr());
        other.clear();
        return *this;
    }

    template <typename U>
    SharedPtr& operator=(SharedPtr<U>&& other) noexcept {
        detach();
        ptr = other.get();
        block = static_cast<BaseBlock*>(other.get_block_ptr());
        other.clear();
        return *this;
    }

    size_t use_count() const {
        return block == nullptr ? 0 : block->shared_cnt;
    }

    void clear() {
        ptr = nullptr;
        block = nullptr;
    }

    void reset() {
        detach();
    }

    template <typename U>
    void reset(U* new_ptr) {
        *this = SharedPtr<T>(new_ptr);
    }

    template <typename U, typename Deleter>
    void reset(U* new_ptr, Deleter deleter) {
        *this = SharedPtr<T>(new_ptr, deleter);
    }

    template <typename U, typename Deleter, typename Alloc>
    void reset(U* new_ptr, Deleter deleter, Alloc alloc) {
        *this = SharedPtr<T>(new_ptr, deleter, alloc);
    }

    BaseBlock* get_block_ptr() const {
        return block;
    }

    T* get() const {
        return ptr;
    }

    T& operator*() {
        return *ptr;
    }

    const T& operator*() const {
        return *ptr;
    }

    T* operator->() {
        return ptr;
    }

    const T* operator->() const {
        return ptr;
    }

    void swap(SharedPtr<T>& other) noexcept {
        std::swap(ptr, other.ptr);
        std::swap(block, other.block);
    }

    ~SharedPtr() {
        detach();
    }
};




template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    using BlockType = shared_and_weak_ptr_block::BlockMakeShared<T>;
    BlockType* new_block = new BlockType(std::forward<Args>(args)...);
    return SharedPtr<T>(new_block);
}

template <typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args) {
    using BlockType = shared_and_weak_ptr_block::BlockAllocateShared<T, Alloc>;
    using Allocator = typename std::allocator_traits<Alloc>:: template rebind_alloc<BlockType>;

    Allocator new_alloc(alloc);
    BlockType* new_block = std::allocator_traits<Allocator>::allocate(new_alloc, 1);
    std::allocator_traits<Allocator>::construct(new_alloc, new_block, new_alloc, std::forward<Args>(args)...);
    return SharedPtr<T>(new_block);
}




template <typename T>
class WeakPtr {
    using BlockType = shared_and_weak_ptr_block::BaseBlock;

    T* ptr;
    BlockType* block;

    void detach() {
        if (block != nullptr) block->detach_weak_ptr();
        clear();
    }

public:
    WeakPtr() : ptr(nullptr), block(nullptr) {}

    template <typename U>
    WeakPtr(const SharedPtr<U>& sh_ptr) : ptr(sh_ptr.get()),
                                          block(static_cast<BlockType*>(sh_ptr.get_block_ptr())) {
        if (block != nullptr) ++block->weak_cnt;
    }

    WeakPtr(const WeakPtr<T>& other) : ptr(other.get()), block(static_cast<BlockType*>(other.get_block_ptr())) {
        if (block != nullptr) ++block->weak_cnt;
    }

    template <typename U>
    WeakPtr(const WeakPtr<U>& other) : ptr(other.get()), block(static_cast<BlockType*>(other.get_block_ptr())) {
        if (block != nullptr) ++block->weak_cnt;
    }

    WeakPtr(WeakPtr<T>&& other) noexcept : ptr(other.get()), block(static_cast<BlockType*>(other.get_block_ptr())) {
        other.clear();
    }

    template <typename U>
    WeakPtr(WeakPtr<U>&& other) noexcept : ptr(other.get()), block(static_cast<BlockType*>(other.get_block_ptr())) {
        other.clear();
    }

    WeakPtr& operator=(const WeakPtr<T>& other) {
        detach();
        ptr = other.get();
        block = static_cast<BlockType*>(other.get_block_ptr());
        if (block != nullptr) ++block->weak_cnt;
        return *this;
    }

    template <typename U>
    WeakPtr& operator=(const WeakPtr<U>& other) {
        detach();
        ptr = other.get();
        block = static_cast<BlockType*>(other.get_block_ptr());
        if (block != nullptr) ++block->weak_cnt;
        return *this;
    }

    WeakPtr& operator=(WeakPtr<T>&& other) noexcept {
        detach();
        ptr = other.get();
        block = static_cast<BlockType*>(other.get_block_ptr());
        other.clear();
        return *this;
    }

    template <typename U>
    WeakPtr& operator=(WeakPtr<U>&& other) noexcept {
        detach();
        ptr = other.get();
        block = static_cast<BlockType*>(other.get_block_ptr());
        other.clear();
        return *this;
    }

    void clear() {
        ptr = nullptr;
        block = nullptr;
    }

    T* get() const {
        return ptr;
    }

    BlockType* get_block_ptr() const {
        return block;
    }

    bool expired() const {
        return !block->shared_cnt;
    }

    SharedPtr<T> lock() const {
        return expired() ? SharedPtr<T>() : SharedPtr<T>(ptr, block);
    }

    size_t use_count() const {
        return ptr == nullptr ? 0 : block->shared_cnt;
    }

    ~WeakPtr() {
        detach();
    }
};

template <typename T>
class EnableSharedFromThis {
    friend class SharedPtr<T>;

    WeakPtr<T> w_ptr;

public:
    SharedPtr<T> shared_from_this() {
        if (w_ptr.get() == nullptr) throw std::bad_weak_ptr();
        return w_ptr.lock();
    }
};

template <typename T>
void SharedPtr<T>::check_enable_shared() {
    if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
        if (block != nullptr) ptr->w_ptr = *this;
    }
}
