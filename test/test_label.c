// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The label check against Unicode's table of well-formed UTF-8, its
// NUL ban and its bound.

#include "label.h"
#include "test_harness.h"

#include <string.h>

static bool Valid(const char* bytes)
{
    return mrhiIsLabelValid(bytes, strlen(bytes));
}

static void TestWellFormed(void)
{
    CHECK(mrhiIsLabelValid(nullptr, 0), "no label");
    CHECK(mrhiIsLabelValid("", 0), "an empty label");
    CHECK(Valid("shadow map"), "ASCII");
    CHECK(Valid("\x01\x7F"), "the ends of ASCII");
    CHECK(Valid("\xC2\x80\xDF\xBF"), "U+0080 and U+07FF");
    CHECK(Valid("\xE0\xA0\x80\xE0\xBF\xBF"), "U+0800 and U+0FFF");
    CHECK(Valid("\xE1\x80\x80\xEC\xBF\xBF"), "U+1000 and U+CFFF");
    CHECK(Valid("\xED\x80\x80\xED\x9F\xBF"), "U+D000 and U+D7FF");
    CHECK(Valid("\xEE\x80\x80\xEF\xBF\xBF"), "U+E000 and U+FFFF");
    CHECK(Valid("\xF0\x90\x80\x80\xF0\xBF\xBF\xBF"), "U+10000 and U+3FFFF");
    CHECK(Valid("\xF1\x80\x80\x80\xF3\xBF\xBF\xBF"), "U+40000 and U+FFFFF");
    CHECK(Valid("\xF4\x80\x80\x80\xF4\x8F\xBF\xBF"), "U+100000 and U+10FFFF");
    CHECK(Valid("g\xC3\xB6lge"), "a two-byte letter inside ASCII");
}

static void TestIllFormed(void)
{
    CHECK(!mrhiIsLabelValid(nullptr, 1), "a length without bytes");
    CHECK(!mrhiIsLabelValid("a\0b", 3), "a NUL");
    CHECK(!Valid("\x80"), "a lone continuation");
    CHECK(!Valid("\xBF"), "the last continuation alone");
    CHECK(!Valid("\xC0\xAF"), "an overlong slash");
    CHECK(!Valid("\xC1\xBF"), "an overlong two-byte form");
    CHECK(!Valid("\xC2\x7F"), "a two-byte form without its continuation");
    CHECK(!Valid("\xC2\xC0"), "a two-byte form past its continuation");
    CHECK(!Valid("\xE0\x9F\xBF"), "an overlong three-byte form");
    CHECK(!Valid("\xE0\xC0\x80"), "past U+0FFF's second byte");
    CHECK(!Valid("\xE1\x7F\x80"), "below a three-byte form's second byte");
    CHECK(!Valid("\xE1\xC0\x80"), "past a three-byte form's second byte");
    CHECK(!Valid("\xED\xA0\x80"), "a surrogate");
    CHECK(!Valid("\xED\x7F\x80"), "below U+D000's second byte");
    CHECK(!Valid("\xEE\x80\x7F"), "a third byte that does not continue");
    CHECK(!Valid("\xEE\x80\xC0"), "a third byte past the continuations");
    CHECK(!Valid("\xF0\x8F\xBF\xBF"), "an overlong four-byte form");
    CHECK(!Valid("\xF0\xC0\x80\x80"), "past U+3FFFF's second byte");
    CHECK(!Valid("\xF4\x90\x80\x80"), "past U+10FFFF");
    CHECK(!Valid("\xF4\x7F\x80\x80"), "below U+100000's second byte");
    CHECK(!Valid("\xF1\x80\x80\x7F"), "a fourth byte that does not continue");
    CHECK(!Valid("\xF5\x80\x80\x80"), "a lead past F4");
    CHECK(!Valid("\xFF"), "FF");
    CHECK(!mrhiIsLabelValid("\xE2\x82\xAC", 2), "a sequence cut by the length");
    CHECK(!mrhiIsLabelValid("\xF0\x9F\x98\x80", 3), "a four-byte sequence cut");
    CHECK(!mrhiIsLabelValid("\xC3", 1), "a lead at the end");
}

static void TestBound(void)
{
    char bytes[MRHI_LABEL_BYTES + 1];
    memset(bytes, 'a', sizeof(bytes));
    CHECK(mrhiIsLabelValid(bytes, MRHI_LABEL_BYTES), "the bound");
    CHECK(!mrhiIsLabelValid(bytes, MRHI_LABEL_BYTES + 1), "past the bound");
    bytes[MRHI_LABEL_BYTES - 2] = (char)0xC3;
    bytes[MRHI_LABEL_BYTES - 1] = (char)0xB6;
    CHECK(mrhiIsLabelValid(bytes, MRHI_LABEL_BYTES), "a sequence ending at the bound");
}

// Text is checked as labels are, at any length.
static void TestText(void)
{
    char bytes[1000];
    memset(bytes, 'a', sizeof(bytes));
    CHECK(mrhiIsTextValid(bytes, sizeof(bytes)), "text past a label's bound");
    bytes[998] = (char)0xC3;
    bytes[999] = (char)0xB6;
    CHECK(mrhiIsTextValid(bytes, sizeof(bytes)), "a sequence ending the text");
    CHECK(!mrhiIsTextValid(bytes, sizeof(bytes) - 1), "a sequence cut by the length");
    bytes[500] = '\0';
    CHECK(!mrhiIsTextValid(bytes, sizeof(bytes)), "a NUL inside");
    CHECK(!mrhiIsTextValid("\xED\xA0\x80", 3), "a surrogate");
}

int main(void)
{
    TestWellFormed();
    TestIllFormed();
    TestBound();
    TestText();
    return s_failures == 0 ? 0 : 1;
}
