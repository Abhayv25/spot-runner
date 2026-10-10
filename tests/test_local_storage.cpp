#include <gtest/gtest.h>

#include "spot_runner/local_storage.h"
#include "spot_runner/util.h"
#include "temp_dir.h"

using namespace spot_runner;
using spot_runner::testing::TempDir;

class LocalStorageTest : public ::testing::Test {
protected:
    TempDir dir_;
    LocalStorage storage_{dir_.str("storage")};

    std::string make_file(const std::string& name, const std::string& contents) {
        const std::string path = dir_.str(name);
        write_file_atomically(path, contents);
        return path;
    }
};

TEST_F(LocalStorageTest, PutThenGetReturnsSameBytes) {
    storage_.put_file("jobs/a/checkpoint.dat", make_file("src", std::string("bin\0ary", 7)));
    ASSERT_TRUE(storage_.get_file("jobs/a/checkpoint.dat", dir_.str("dst")));
    EXPECT_EQ(read_file(dir_.str("dst")), std::string("bin\0ary", 7));
}

TEST_F(LocalStorageTest, GetMissingKeyReturnsFalse) {
    EXPECT_FALSE(storage_.get_file("jobs/none/checkpoint.dat", dir_.str("dst")));
    EXPECT_FALSE(std::filesystem::exists(dir_.str("dst")));
}

TEST_F(LocalStorageTest, PutOverwritesAndLeavesNoTempFiles) {
    storage_.put_file("k", make_file("v1", "one"));
    storage_.put_file("k", make_file("v2", "two"));
    storage_.get_file("k", dir_.str("out"));
    EXPECT_EQ(read_file(dir_.str("out")), "two");
    for (const auto& entry : std::filesystem::directory_iterator(dir_.str("storage"))) {
        EXPECT_EQ(entry.path().filename().string().find(".tmp"), std::string::npos);
    }
}

TEST_F(LocalStorageTest, ExistsAndRemove) {
    storage_.put_file("k", make_file("v", "x"));
    EXPECT_TRUE(storage_.exists("k"));
    storage_.remove("k");
    EXPECT_FALSE(storage_.exists("k"));
    EXPECT_NO_THROW(storage_.remove("k"));
}

TEST_F(LocalStorageTest, RejectsKeysThatEscapeTheRoot) {
    const auto src = make_file("v", "x");
    EXPECT_THROW(storage_.put_file("../../etc/evil", src), std::invalid_argument);
    EXPECT_THROW(storage_.put_file("/abs/path", src), std::invalid_argument);
    EXPECT_THROW(storage_.put_file("", src), std::invalid_argument);
}

TEST(StorageKeys, AreNamespacedByJob) {
    EXPECT_EQ(checkpoint_key("job-1"), "jobs/job-1/checkpoint.dat");
    EXPECT_EQ(result_key("job-1"), "jobs/job-1/result.dat");
}
