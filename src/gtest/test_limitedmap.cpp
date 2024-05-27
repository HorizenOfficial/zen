// Copyright (c) 2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
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

    lm.insert(std::make_pair("d", 5));
    ASSERT_EQ(lm.size(), 2);
    ASSERT_TRUE(lm.find("c") != lm.end());
    ASSERT_TRUE(lm.find("d") != lm.end());
    ASSERT_EQ(lm.find("d")->second, 4);
}

TEST(LimitedMap, Update) {
    LimitedMap<const char*, int> lm(2);

    lm.insert(std::make_pair("a", 1));
    lm.update(lm.find("a"), 2);
    ASSERT_EQ(lm.size(), 1);
    ASSERT_TRUE(lm.find("a") != lm.end());
    ASSERT_EQ(lm.find("a")->second, 2);

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

    lm.erase("a");
    ASSERT_EQ(lm.size(), 1);
    ASSERT_TRUE(lm.find("c") != lm.end());

    lm.erase("c");
    ASSERT_EQ(lm.size(), 0);
}
