#include <gtest/gtest.h>
#include "limitedmap.h"

TEST(LimitedMap, Insertion) {
    LimitedMap<const char*, int> lm(2);

    lm.insert(std::make_pair("a", 1));
    // a is already in map, not updated here
    lm.insert(std::make_pair("a", 2));
    ASSERT_EQ(lm.size(), 1);
    ASSERT_TRUE(lm.find("a") != lm.end());
    ASSERT_EQ(lm.find("a")->second, 1);

    lm.insert(std::make_pair("b", 2));
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("a") != lm.end());
    ASSERT_TRUE(lm.find("b") != lm.end());

    lm.insert(std::make_pair("c", 3));
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("b") != lm.end());
    ASSERT_TRUE(lm.find("c") != lm.end());

    // d should not be inserted, given its low value
    lm.insert(std::make_pair("d", 1));
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("b") != lm.end());
    ASSERT_TRUE(lm.find("c") != lm.end());

    lm.insert(std::make_pair("d", 4));
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("c") != lm.end());
    ASSERT_TRUE(lm.find("d") != lm.end());
}

TEST(LimitedMap, Update) {
    LimitedMap<const char*, int> lm(0);
    lm.insert(std::make_pair("a", 1));
    ASSERT_EQ(lm.size(), 0);

    lm.max_size(1);
    lm.insert(std::make_pair("a", 1));
    lm.update(lm.find("a"), 2);
    ASSERT_EQ(lm.size(), 1);
    ASSERT_TRUE(lm.find("a") != lm.end());
    ASSERT_EQ(lm.find("a")->second, 2);

    lm.max_size(2);
    lm.insert(std::make_pair("b", 1));
    lm.update(lm.find("b"), 2);
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("a") != lm.end());
    ASSERT_TRUE(lm.find("b") != lm.end());
    ASSERT_EQ(lm.find("b")->second, 2);
}

TEST(LimitedMap, Deletion) {
    LimitedMap<const char*, int> lm(3);

    lm.insert(std::make_pair("a", 1));
    lm.insert(std::make_pair("b", 2));
    lm.insert(std::make_pair("c", 3));

    lm.erase("b");
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("a") != lm.end());
    ASSERT_TRUE(lm.find("c") != lm.end());

    lm.max_size(1);
    ASSERT_EQ(lm.size(), 1);
    ASSERT_TRUE(lm.find("c") != lm.end());
}
