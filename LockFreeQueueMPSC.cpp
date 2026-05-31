#include <memory>
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>

#include "LockFreeQueueMPSC.hpp"

constexpr int NUM_PRODUCERS = 4;
constexpr int WORKLOAD = 1000;
constexpr int NUMA_NODE_0 = 0;
constexpr int NUMA_NODE_1 = 1;

void pinThreadToCore(int threadIndex, int numaNode) {
    /*
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int cores_per_node = std::thread::hardware_concurrency();
    int core_id = (threadIndex % cores_per_node) + (numaNode * cores_per_node);
    assert(core_id < std::thread::hardware_concurrency());
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    */
}

int main() {
    LockFreeQueueMPSC<int> queue;
    std::vector<std::thread> threads;

    // Start producer threads (MPSC)
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        threads.emplace_back([i, &queue]() {
            pinThreadToCore(i, NUMA_NODE_0);
            for (int j = 0; j < WORKLOAD; ++j) {
                queue.enqueue(j);
            }
        });
    }

    // Single consumer thread (IMPORTANT difference)
    std::thread consumer([&queue]() {
        pinThreadToCore(0, NUMA_NODE_1);

        int value;
        int pops = 0;

        const int total = NUM_PRODUCERS * WORKLOAD;

        while (pops < total) {
            if (queue.dequeue(value)) {
                ++pops;
            }
        }
    });

    // Join producers
    for (auto& t : threads) {
        t.join();
    }

    // Join consumer
    consumer.join();

    std::cout << "MPSC Completed.\n";
    return 0;
}
