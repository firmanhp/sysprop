#include "validation.h"

#include <string_view>

#include <gtest/gtest.h>
#include <sysprop/sysprop.h>

using sysprop::internal::ValidateKey;
using sysprop::internal::ValidateValue;

// ── ValidateKey ───────────────────────────────────────────────────────────────

TEST(ValidateKey, AcceptsSimpleNames) {
  EXPECT_EQ(SYSPROP_OK, ValidateKey("ro.build.version"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("persist.wifi.ssid"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("sys.boot_completed"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("a"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("A1-B_2.c"));
}

TEST(ValidateKey, AcceptsAllAllowedCharacters) {
  EXPECT_EQ(SYSPROP_OK, ValidateKey("abcdefghijklmnopqrstuvwxyz"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("0123456789"));
  EXPECT_EQ(SYSPROP_OK, ValidateKey("a.b-c_d"));
}

TEST(ValidateKey, RejectsEmpty) { EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("")); }

TEST(ValidateKey, RejectsLeadingDot) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(".leading.dot"));
}

TEST(ValidateKey, RejectsTrailingDot) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("trailing.dot."));
}

TEST(ValidateKey, RejectsConsecutiveDots) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("double..dot"));
}

TEST(ValidateKey, RejectsSpaces) { EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("has space")); }

TEST(ValidateKey, RejectsSlash) { EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("has/slash")); }

TEST(ValidateKey, RejectsNullByte) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\0b", 3)));
}

TEST(ValidateKey, RejectsTooLong) {
  EXPECT_EQ(SYSPROP_ERR_KEY_TOO_LONG, ValidateKey(std::string(SYSPROP_MAX_KEY_LENGTH, 'a')));
}

TEST(ValidateKey, AcceptsMaxLength) {
  EXPECT_EQ(SYSPROP_OK, ValidateKey(std::string(SYSPROP_MAX_KEY_LENGTH - 1, 'a')));
}

// ── ValidateValue ─────────────────────────────────────────────────────────────

TEST(ValidateValue, AcceptsEmpty) { EXPECT_EQ(SYSPROP_OK, ValidateValue("")); }

TEST(ValidateValue, AcceptsNormalString) {
  EXPECT_EQ(SYSPROP_OK, ValidateValue("hello world"));
  EXPECT_EQ(SYSPROP_OK, ValidateValue("192.168.1.1"));
}

TEST(ValidateValue, AcceptsMaxLength) {
  EXPECT_EQ(SYSPROP_OK, ValidateValue(std::string(SYSPROP_MAX_VALUE_LENGTH - 1, 'x')));
}

TEST(ValidateValue, RejectsTooLong) {
  EXPECT_EQ(SYSPROP_ERR_VALUE_TOO_LONG,
            ValidateValue(std::string(SYSPROP_MAX_VALUE_LENGTH, 'x')));
}

TEST(ValidateValue, RejectsEmbeddedNull) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateValue(std::string_view("hel\0lo", 6)));
}
