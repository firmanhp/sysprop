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

// A key consisting solely of a hyphen is valid (hyphen is an allowed charset char).
TEST(ValidateKey, AcceptsSingleHyphen) { EXPECT_EQ(SYSPROP_OK, ValidateKey("-")); }

// A key consisting solely of digits is valid.
TEST(ValidateKey, AcceptsDigitsOnly) { EXPECT_EQ(SYSPROP_OK, ValidateKey("42")); }

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

TEST(ValidateKey, RejectsSingleDot) { EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(".")); }

TEST(ValidateKey, RejectsAtSign) { EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("a@b")); }

TEST(ValidateKey, RejectsPathTraversal) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("../etc/passwd"));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("a/b"));
}

// ── Shell-injection attempts ──────────────────────────────────────────────────

TEST(ValidateKey, RejectsShellMetacharacters) {
  for (const char* k :
       {"key|pipe", "key;semi", "key&bg", "key$(subshell)", "key`backtick`", "key>redir",
        "key<redir", "key(open", "key)close", "key*glob", "key?wild"}) {
    EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(k)) << "key=" << k;
  }
}

// ── Control-character injection ───────────────────────────────────────────────

TEST(ValidateKey, RejectsControlCharacters) {
  // Newline — log-injection / config-parser splitting
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\nb", 3)));
  // Carriage return
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\rb", 3)));
  // Tab
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\tb", 3)));
  // SOH (0x01) — common in binary injection probes
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\x01"
                                                                  "b",
                                                                  3)));
  // DEL (0x7f)
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\x7f"
                                                                  "b",
                                                                  3)));
  // ESC (0x1b) — terminal-escape injection
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("a\x1b"
                                                                  "b",
                                                                  3)));
}

// ── High-byte / Unicode injection ─────────────────────────────────────────────

TEST(ValidateKey, RejectsHighBytes) {
  // UTF-8 multi-byte sequence ("café")
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("caf\xc3\xa9", 5)));
  // Lone continuation byte
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("k\x80"
                                                                  "ey",
                                                                  4)));
  // 0xff — invalid in any UTF-8 encoding
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("k\xff"
                                                                  "ey",
                                                                  4)));
}

// ── Backslash (Windows-path injection) ───────────────────────────────────────

TEST(ValidateKey, RejectsBackslash) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("ro.build\\version"));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey("..\\..\\etc\\passwd"));
}

// ── Null-byte injection at every position ─────────────────────────────────────

TEST(ValidateKey, RejectsNullByteAtStart) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("\0key", 4)));
}

TEST(ValidateKey, RejectsNullByteAtEnd) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(std::string_view("key\0", 4)));
}

// ── Dot-dot ───────────────────────────────────────────────────────────────────

TEST(ValidateKey, RejectsDotDot) {
  // Bare ".." would escape to parent directory if validation were missing.
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, ValidateKey(".."));
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
  EXPECT_EQ(SYSPROP_ERR_VALUE_TOO_LONG, ValidateValue(std::string(SYSPROP_MAX_VALUE_LENGTH, 'x')));
}

TEST(ValidateValue, RejectsEmbeddedNull) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_VALUE, ValidateValue(std::string_view("hel\0lo", 6)));
}

TEST(ValidateValue, RejectsNullByteAtStart) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_VALUE, ValidateValue(std::string_view("\0value", 6)));
}

TEST(ValidateValue, RejectsNullByteAtEnd) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_VALUE, ValidateValue(std::string_view("value\0", 6)));
}

TEST(ValidateValue, AcceptsSpacesAndPunctuation) {
  EXPECT_EQ(SYSPROP_OK, ValidateValue("hello world"));
  EXPECT_EQ(SYSPROP_OK, ValidateValue("!@#$%^&*()"));
  EXPECT_EQ(SYSPROP_OK, ValidateValue("/path/to/something"));
}

TEST(ValidateValue, AcceptsNumericStrings) {
  EXPECT_EQ(SYSPROP_OK, ValidateValue("0"));
  EXPECT_EQ(SYSPROP_OK, ValidateValue("-42"));
  EXPECT_EQ(SYSPROP_OK, ValidateValue("3.14"));
}
