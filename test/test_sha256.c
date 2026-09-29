// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// SHA-256 against the Secure Hash Standard's examples and the padding's
// edges.

#include "sha256.h"
#include "test_harness.h"

#include <string.h>

// Whether the digest of the bytes has the hexadecimal form given.
static bool Hashes(const void* bytes, size_t length, const char* hex)
{
    uint8_t digest[32];
    mrhiSha256(bytes, length, digest);
    char text[65];
    static const char kDigits[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i)
    {
        text[i * 2] = kDigits[digest[i] >> 4];
        text[i * 2 + 1] = kDigits[digest[i] & 15];
    }
    text[64] = '\0';
    return strcmp(text, hex) == 0;
}

int main(void)
{
    CHECK(Hashes(nullptr, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
          "nothing");
    CHECK(Hashes("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "abc");
    const char* twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(Hashes(twoBlocks, strlen(twoBlocks),
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
          "56 bytes, padded into a second block");
    char a[1000];
    memset(a, 'a', sizeof(a));
    CHECK(Hashes(a, 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"),
          "55 bytes, padded in one block");
    CHECK(Hashes(a, 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"),
          "one whole block");
    CHECK(Hashes(a, 1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"),
          "a thousand bytes");
    return s_failures == 0 ? 0 : 1;
}
