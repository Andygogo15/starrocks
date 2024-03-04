// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "block_cache/block_cache.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "common/logging.h"
#include "common/statusor.h"
#include "fs/fs_util.h"
#include "storage/options.h"

namespace starrocks {

static const size_t block_size = 1024 * 1024;

class BlockCacheTest : public ::testing::Test {
protected:
    static void SetUpTestCase() { ASSERT_TRUE(fs::create_directories("./block_disk_cache").ok()); }

    static void TearDownTestCase() { ASSERT_TRUE(fs::remove_all("./block_disk_cache").ok()); }

    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BlockCacheTest, copy_to_iobuf) {
    // Create an iobuffer which contains 3 blocks
    const size_t buf_block_size = 100;
    void* data1 = malloc(buf_block_size);
    void* data2 = malloc(buf_block_size);
    void* data3 = malloc(buf_block_size);
    memset(data1, 1, buf_block_size);
    memset(data2, 2, buf_block_size);
    memset(data3, 3, buf_block_size);

    IOBuffer buffer;
    buffer.append_user_data(data1, buf_block_size, nullptr);
    buffer.append_user_data(data2, buf_block_size, nullptr);
    buffer.append_user_data(data3, buf_block_size, nullptr);

    // Copy the last 150 bytes of iobuffer to a target buffer
    const off_t offset = 150;
    const size_t size = 150;
    char result[size] = {0};
    buffer.copy_to(result, size, offset);

    // Check the target buffer content
    char expect[size] = {0};
    memset(expect, 2, 50);
    memset(expect + 50, 3, 100);
    ASSERT_EQ(memcmp(result, expect, size), 0);
}

TEST_F(BlockCacheTest, parse_cache_space_paths) {
    const std::string cwd = std::filesystem::current_path().string();
    const std::string s_normal_path = fmt::format("{}/block_disk_cache/cache1;{}/block_disk_cache/cache2", cwd, cwd);
    std::vector<std::string> paths;
    ASSERT_TRUE(parse_conf_block_cache_paths(s_normal_path, &paths).ok());
    ASSERT_EQ(paths.size(), 2);

    paths.clear();
    const std::string s_space_path = fmt::format(" {}/block_disk_cache/cache3 ; {}/block_disk_cache/cache4 ", cwd, cwd);
    ASSERT_TRUE(parse_conf_block_cache_paths(s_space_path, &paths).ok());
    ASSERT_EQ(paths.size(), 2);

    paths.clear();
    const std::string s_empty_path = fmt::format("//;{}/block_disk_cache/cache4 ", cwd, cwd);
    ASSERT_FALSE(parse_conf_block_cache_paths(s_empty_path, &paths).ok());
    ASSERT_EQ(paths.size(), 1);

    paths.clear();
    const std::string s_invalid_path = fmt::format(" /block_disk_cache/cache5;{}/+/cache6", cwd, cwd);
    ASSERT_FALSE(parse_conf_block_cache_paths(s_invalid_path, &paths).ok());
    ASSERT_EQ(paths.size(), 0);
}

#ifdef WITH_STARCACHE
TEST_F(BlockCacheTest, hybrid_cache) {
    std::unique_ptr<BlockCache> cache(new BlockCache);
    const size_t block_size = 1024 * 1024;

    CacheOptions options;
    options.mem_space_size = 10 * 1024 * 1024;
    size_t quota = 500 * 1024 * 1024;
    options.disk_spaces.push_back({.path = "./block_disk_cache", .size = quota});
    options.block_size = block_size;
    options.max_concurrent_inserts = 100000;
    options.engine = "starcache";
    Status status = cache->init(options);
    ASSERT_TRUE(status.ok());

    const size_t batch_size = block_size - 1234;
    const size_t rounds = 20;
    const std::string cache_key = "test_file";

    // write cache
    off_t offset = 0;
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        std::string value(batch_size, ch);
        Status st = cache->write_cache(cache_key + std::to_string(i), 0, batch_size, value.c_str());
        ASSERT_TRUE(st.ok());
        offset += batch_size;
    }

    // read cache
    offset = 0;
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        std::string expect_value(batch_size, ch);
        char value[batch_size] = {0};
        auto res = cache->read_cache(cache_key + std::to_string(i), 0, batch_size, value);
        ASSERT_TRUE(res.status().ok());
        ASSERT_EQ(memcmp(value, expect_value.c_str(), batch_size), 0);
        offset += batch_size;
    }

    // remove cache
    char value[1024] = {0};
    status = cache->remove_cache(cache_key, 0, batch_size);
    ASSERT_TRUE(status.ok());

    auto res = cache->read_cache(cache_key, 0, batch_size, value);
    ASSERT_TRUE(res.status().is_not_found());

    // not found
    res = cache->read_cache(cache_key, block_size * 1000, batch_size, value);
    ASSERT_TRUE(res.status().is_not_found());

    cache->shutdown();
}

TEST_F(BlockCacheTest, write_with_overwrite_option) {
    std::unique_ptr<BlockCache> cache(new BlockCache);
    const size_t block_size = 1024 * 1024;

    CacheOptions options;
    options.mem_space_size = 20 * 1024 * 1024;
    options.block_size = block_size;
    options.max_concurrent_inserts = 100000;
    options.engine = "starcache";
    Status status = cache->init(options);
    ASSERT_TRUE(status.ok());

    const size_t cache_size = 1024;
    const std::string cache_key = "test_file";

    std::string value(cache_size, 'a');
    Status st = cache->write_cache(cache_key, 0, cache_size, value.c_str());
    ASSERT_TRUE(st.ok());

    std::string value2(cache_size, 'b');
    st = cache->write_cache(cache_key, 0, cache_size, value2.c_str(), 0, true);
    ASSERT_TRUE(st.ok());

    char rvalue[cache_size] = {0};
    auto res = cache->read_cache(cache_key, 0, cache_size, rvalue);
    ASSERT_TRUE(res.status().ok());
    std::string expect_value(cache_size, 'b');
    ASSERT_EQ(memcmp(rvalue, expect_value.c_str(), cache_size), 0);

    std::string value3(cache_size, 'c');
    st = cache->write_cache(cache_key, 0, cache_size, value3.c_str(), 0, false);
    ASSERT_TRUE(st.is_already_exist());

    cache->shutdown();
}
#endif

#ifdef WITH_CACHELIB
TEST_F(BlockCacheTest, custom_lru_insertion_point) {
    std::unique_ptr<BlockCache> cache(new BlockCache);
    const size_t block_size = 1024 * 1024;

    CacheOptions options;
    options.mem_space_size = 20 * 1024 * 1024;
    options.block_size = block_size;
    options.max_concurrent_inserts = 100000;
    options.engine = "cachelib";
    // insert in the 1/2 of the lru list
    options.lru_insertion_point = 1;
    Status status = cache->init(options);
    ASSERT_TRUE(status.ok());

    const size_t rounds = 20;
    const size_t batch_size = block_size;
    const std::string cache_key = "test_file";
    // write cache
    // only 12 blocks can be cached
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        std::string value(batch_size, ch);
        Status st = cache->write_cache(cache_key + std::to_string(i), 0, batch_size, value.c_str());
        ASSERT_TRUE(st.ok());
    }

    // read cache
    // with the 1/2 lru insertion point, the test_file1 items will not be evicted
    char value[batch_size] = {0};
    auto res = cache->read_cache(cache_key + std::to_string(1), 0, batch_size, value);
    ASSERT_TRUE(res.status().ok());

    cache->shutdown();
}
#endif

} // namespace starrocks
