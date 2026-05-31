#include <atomic>
#include <thread>
#include <immintrin.h>

#ifndef hardware_destructive_interference_size
#define hardware_destructive_interference_size 64
#endif

constexpr size_t CACHE_LINE_SIZE = hardware_destructive_interference_size;


// MPSC Queue
template <typename T>
class LockFreeQueueMPSC {
private:
    struct alignas(CACHE_LINE_SIZE) Node 
    {
        T data;
        std::atomic<Node*> next;

        explicit Node(T const& value) : data(value), next(nullptr) {}
        Node() : next(nullptr) {}
    };

    alignas(CACHE_LINE_SIZE) std::atomic<Node*> head;
    alignas(CACHE_LINE_SIZE) std::atomic<Node*> tail;

public:

    LockFreeQueueMPSC(const LockFreeQueueMPSC&) = delete;
    LockFreeQueueMPSC& operator=(const LockFreeQueueMPSCMPSC&) = delete;
    LockFreeQueueMPSC(LockFreeQueueMPSC&&) = delete;
    LockFreeQueueMPSC& operator=(LockFreeQueueMPSC&&) = delete;

    LockFreeQueueMPSC() {
        Node* dummy = new Node();
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    // =========================
    // Multiple Producers
    // =========================
    void enqueue(T const& value) {
        Node* new_node = new Node(value);
        new_node->next.store(nullptr, std::memory_order_relaxed);

        while (true) {
            Node* old_tail = tail.load(std::memory_order_acquire);

            Node* next = nullptr;

            if (old_tail->next.compare_exchange_weak(
                    next, new_node,
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                tail.compare_exchange_strong(
                    old_tail, new_node,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed);

                return;
            }

            // tail is lagging behind → help advance
            tail.compare_exchange_strong(
                old_tail,
                old_tail->next.load(std::memory_order_relaxed),
                std::memory_order_relaxed,
                std::memory_order_relaxed);

            #ifdef __x86_64__
            _mm_pause();
            #else
            std::this_thread::yield();
            #endif
        }
    }

    // =========================
    // Single Consumer (NO CAS)
    // =========================
    bool dequeue(T& out) {
        Node* old_head = head.load(std::memory_order_relaxed);
        Node* old_head_next = old_head->next.load(std::memory_order_acquire);

        if (old_head_next == nullptr) {
            return false;
        }

        out = std::move(old_head_next->data);

        head.store(old_head_next, std::memory_order_relaxed);

        delete old_head; // safe because single consumer

        return true;
    }

    bool empty() const {
        Node* h = head.load(std::memory_order_relaxed);
        return h->next.load(std::memory_order_relaxed) == nullptr;
    }

    ~LockFreeQueueMPSC() {
        Node* current = head.load(std::memory_order_relaxed);
        while (current) {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }
};
