// Host-side tests for the single-producer/single-consumer ring used between
// pipeline stages. Run with: pio test -e native
#include <unity.h>
#include <atomic>
#include <thread>
#include "spsc_ring.h"

using mic::SpscRing;

void setUp() {}
void tearDown() {}

void test_empty_ring_pops_nothing() {
    SpscRing<int, 4> r;
    int v = 0;
    TEST_ASSERT_TRUE(r.empty());
    TEST_ASSERT_FALSE(r.pop(v));
    TEST_ASSERT_EQUAL_UINT32(0, r.size());
}

void test_capacity_is_n_and_push_fails_when_full() {
    SpscRing<int, 4> r;
    for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE(r.push(i));
    TEST_ASSERT_FALSE(r.push(99));
    TEST_ASSERT_EQUAL_UINT32(4, r.size());
}

void test_fifo_order_and_wraparound() {
    SpscRing<int, 4> r;
    int v = -1;
    for (int round = 0; round < 10; ++round) {
        TEST_ASSERT_TRUE(r.push(round * 10 + 1));
        TEST_ASSERT_TRUE(r.push(round * 10 + 2));
        TEST_ASSERT_TRUE(r.pop(v));
        TEST_ASSERT_EQUAL_INT(round * 10 + 1, v);
        TEST_ASSERT_TRUE(r.pop(v));
        TEST_ASSERT_EQUAL_INT(round * 10 + 2, v);
    }
    TEST_ASSERT_TRUE(r.empty());
}

void test_two_threads_transfer_all_items_in_order() {
    SpscRing<uint32_t, 8> r;
    constexpr uint32_t kItems = 200000;
    std::atomic<bool> ok{true};
    std::thread consumer([&] {
        uint32_t expect = 0, v = 0;
        while (expect < kItems) {
            if (r.pop(v)) {
                if (v != expect) ok = false;
                ++expect;
            }
        }
    });
    for (uint32_t i = 0; i < kItems;) {
        if (r.push(i)) ++i;
    }
    consumer.join();
    TEST_ASSERT_TRUE(ok.load());
    TEST_ASSERT_TRUE(r.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring_pops_nothing);
    RUN_TEST(test_capacity_is_n_and_push_fails_when_full);
    RUN_TEST(test_fifo_order_and_wraparound);
    RUN_TEST(test_two_threads_transfer_all_items_in_order);
    return UNITY_END();
}
