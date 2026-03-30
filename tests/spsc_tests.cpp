#include <gtest/gtest.h>
#include "SpscRingBuffer.hpp"
#include "vector.hpp"

// --- SpscRingBuffer tests ---

TEST(SpscRingBuffer, PopFromEmptyFails) {
    SpscRingBuffer<int> q(8);
    int x = 0;
    EXPECT_FALSE(q.try_pop(x));
}

TEST(SpscRingBuffer, PushThenPopFIFOOrder) {
    SpscRingBuffer<int> q(8);

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.try_push(i));
    }

    for (int i = 0; i < 4; ++i) {
        int x = -1;
        ASSERT_TRUE(q.try_pop(x));
        EXPECT_EQ(x, i);
    }
}

TEST(SpscRingBuffer, PushToFullFails) {
    SpscRingBuffer<int> q(4);

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.try_push(i));
    }
    EXPECT_FALSE(q.try_push(99));
}

TEST(SpscRingBuffer, EmptyAndFullState) {
    SpscRingBuffer<int> q(4);
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());

    for (int i = 0; i < 4; ++i) q.try_push(i);
    EXPECT_FALSE(q.empty());
    EXPECT_TRUE(q.full());

    int x;
    q.try_pop(x);
    EXPECT_FALSE(q.full());
}

// --- Vector tests ---

TEST(Vector, PushBackAndCopyPreservesOrder) {
    Vector<int> v(4);

    for (int i = 0; i < 16; ++i) {
        v.push_back(i);
    }

    Vector<int> v2 = v;

    for (int i = 15; i >= 0; --i) {
        EXPECT_EQ(v2.back(), i);
        v2.pop_back();
    }
    EXPECT_TRUE(v2.empty());
}

TEST(Vector, BackOnEmptyThrows) {
    Vector<int> v(4);
    EXPECT_THROW(v.back(), std::runtime_error);
}

TEST(Vector, FrontOnEmptyThrows) {
    Vector<int> v(4);
    EXPECT_THROW(v.front(), std::runtime_error);
}

TEST(Vector, GrowthPreservesElements) {
    Vector<int> v(2);
    for (int i = 0; i < 8; ++i) {
        v.push_back(i);
    }
    EXPECT_EQ(v.size(), 8u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], i);
    }
}
