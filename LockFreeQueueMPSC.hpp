#include <atomic>
#include <cassert>
#include <thread>
#include <immintrin.h> // Required for _mm_pause()

#ifndef hardware_destructive_interference_size
#define hardware_destructive_interference_size 64
#endif

constexpr size_t CACHE_LINE_SIZE = hardware_destructive_interference_size;


//Michael & Scott queue
template <typename T>
class LockFreeQueueMPMC {
private:
    struct alignas(CACHE_LINE_SIZE) Node 
   {
        T data;
        std::atomic<Node*> next;

        explicit Node(T const& value) : data(value), next(nullptr) {}
        Node() : next(nullptr) {} // Dummy node constructor for simplified logic
    };

    alignas(CACHE_LINE_SIZE) std::atomic<Node*> head; // Points to dummy or oldest unconsumed node
    alignas(CACHE_LINE_SIZE) std::atomic<Node*> tail; // Points to newest node for enqueue

public:
    
    LockFreeQueueMPMC(const LockFreeQueueMPMC&) = delete;
    LockFreeQueueMPMC& operator=(const LockFreeQueueMPMC&) = delete;
    LockFreeQueueMPMC(LockFreeQueueMPMC&&) = delete;
    LockFreeQueueMPMC& operator=(LockFreeQueueMPMC&&) = delete;

    LockFreeQueueMPMC() 
    {
        // Initialize queue with dummy node to simplify empty queue handling
        Node* dummy = new Node();
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    // Enqueue operation - Append at tail
    // Enqueue operation – Michael & Scott MPMC queue
    // Assumes construction (dummy node) completes before any threads start
    void enqueue(T const& value) {
        Node* new_node = new Node(value);
        new_node->next.store(nullptr, std::memory_order_relaxed);

        while (true) {
            // 1. Acquire: ensures old_tail is fully initialized before dereferencing
            Node* old_tail = tail.load(std::memory_order_acquire);
            // 2. Acquire pairs with the release CAS that linked next
            Node* old_tail_next = old_tail->next.load(std::memory_order_acquire);

            // If tail is pointing to the real end
            if (old_tail_next == nullptr) {
                // 3. CAS to link new node at end of the list
                //    Publish new_node by linking it into old_tail->next
                // This publishes: the new node, the node’s data, all prior writes to that node
                // This is the ONLY real publish point
                if (old_tail->next.compare_exchange_weak(old_tail_next, new_node,
                        std::memory_order_release, // Publish new_node by linking it into the queue.
                                                   // Consumers can reach new_node only after this release CAS succeeds.
                        std::memory_order_relaxed)) // failure = retry
                {
                    //4. Try to swing tail to the new node (not mandatory but improves progress)
                    // Advance tail (optimization; not part of correctness)
                    // CAS may fail spuriously, so strong CAS is used for simplicity
                    // This update is only a performance hint; correctness is unaffected
                    // because enqueue correctness is guaranteed by old_tail->next CAS (release) above
                    // This updates only a pointer value (tail hint update),
                    // it does NOT publish the node or its data and does NOT participate in synchronization.
                    // The real publication happens via old_tail->next CAS (release) above
                    tail.compare_exchange_strong(old_tail, new_node,
                        std::memory_order_relaxed, 
                        std::memory_order_relaxed); 
                    return;
                }
                
                //CAS failed → means another thread has made progress and appended a node. 
                
            } else {
                // 5. Someone already appended a node, but tail still points to an older node.
                // Tail is behind → help advance it (optimization only)
                //compare_exchange_weak may fail spuriously and need looping, so use compare_exchange_strong for simplicity here
                tail.compare_exchange_strong(old_tail, old_tail_next,
                    std::memory_order_relaxed, 
                    std::memory_order_relaxed);
            }

            // Optional: reduce contention with brief pause
            #ifdef __x86_64__
            _mm_pause();  // On x86, better than yielding
            #else
            std::this_thread::yield();
            #endif
        }
    }

    // Dequeue operation - Remove from head
    //This is the ONLY function which is different between MPMC and MPSC code
    bool dequeue(T& out) {

        // ===================== ONLY CHANGE FOR MPSC =====================
        // Single consumer → NO CAS, NO LOOP

        Node* old_head = head.load(std::memory_order_acquire);
        Node* old_head_next = old_head->next.load(std::memory_order_acquire);

        if (old_head_next == nullptr) {
            // Queue is logically empty: only dummy present
            return false; 
        }

        // safe publish of value
        out = std::move(old_head_next->data); //for complex T

        // move head forward (single consumer → safe without CAS)
        // We do not need memory_order_release because head is not used for inter-thread synchronization.
        // All visibility guarantees are already provided by:
        // producer: release on old_tail->next CAS
        // consumer: acquire on old_head->next load
        // head is only a single-consumer cursor and does not participate in the happens-before chain.
        head.store(old_head_next, std::memory_order_relaxed);

        //Without hazard pointers, epoch reclamation, RCU, etc., this queue leaks memory.
        //ABA is not triggered in practice as we do not reclaim and memory will not be reused
        //delete old_head; 

        return true;

        // ===============================================================
    }

    // Optional: relaxed check for emptiness
    bool empty() const {

        //empty() is a speculative check and doesn’t need strong ordering.
        
        Node* h = head.load(std::memory_order_relaxed);
        return h->next.load(std::memory_order_relaxed) == nullptr;
    }

    ~LockFreeQueueMPMC() {
        //Node* current = head.exchange(nullptr, std::memory_order_acquire);
        //Destructor is single-threaded by contract: 
        //since no other threads should be accessing the queue during destruction,
        //We can safely use relaxed memory order here because we are not concerned with visibility or synchronization
        //of the head pointer across threads at this point.
        
        Node* current = head.load(std::memory_order_relaxed);
        while (current) {
           //Node* next = current->next.load(std::memory_order_acquire);
           Node* next = current->next.load(std::memory_order_relaxed);
           delete current;
           current = next;
       }
    }
};
