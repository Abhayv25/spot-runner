#include <gtest/gtest.h>

#include "spot_runner/util.h"
#include "temp_dir.h"

using namespace spot_runner;

TEST(Util, Base64MatchesRfc4648Vectors) {
    EXPECT_EQ(base64_encode(""), "");
    EXPECT_EQ(base64_encode("f"), "Zg==");
    EXPECT_EQ(base64_encode("fo"), "Zm8=");
    EXPECT_EQ(base64_encode("foo"), "Zm9v");
    EXPECT_EQ(base64_encode("foob"), "Zm9vYg==");
    EXPECT_EQ(base64_encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(base64_encode("foobar"), "Zm9vYmFy");
}

TEST(Util, FormatsIso8601InUtc) {
    EXPECT_EQ(format_iso8601(0), "1970-01-01T00:00:00.000Z");
    EXPECT_EQ(format_iso8601(1760114401123), "2025-10-10T16:40:01.123Z");
}

TEST(Util, TailLines) {
    EXPECT_EQ(tail_lines("a\nb\nc\n", 2), "b\nc\n");
    EXPECT_EQ(tail_lines("a\nb\nc", 5), "a\nb\nc");
    EXPECT_EQ(tail_lines("", 3), "");
}

TEST(Util, RandomHexHasRequestedLengthAndVaries) {
    const auto a = random_hex(8);
    EXPECT_EQ(a.size(), 16U);
    EXPECT_NE(a, random_hex(8));
}

TEST(Util, AtomicWriteCreatesParentsAndReplaces) {
    spot_runner::testing::TempDir dir;
    const auto path = dir.path() / "nested" / "file.txt";
    write_file_atomically(path, "first");
    write_file_atomically(path, "second");
    EXPECT_EQ(read_file(path), "second");
    int entries = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(path.parent_path())) {
        ++entries;
    }
    EXPECT_EQ(entries, 1);  // no temp files left behind
}
