///
///\file libsodium_ut_auth2.h
///\brief Unit tests for the sodium library auth2 functions.
///
///\details
///
///\author Dmitriy Kiryanov dmitriyk@aethernet.io
///\version 1.0.0
///\date  11.07.2024
///
#ifndef THIRD_PARTY_LIBSODIUM_UT_AUTH2_H
#define THIRD_PARTY_LIBSODIUM_UT_AUTH2_H

#include "cmptest.h"

namespace auth2
{
    #define TEST_NAME9 "auth2"    

    static unsigned char key[32] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
        0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };

    static unsigned char c[50] = { 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
                                   0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
                                   0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
                                   0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
                                   0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
                                   0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
                                   0xcd, 0xcd };

    static unsigned char a[32];

    void _libsodium_ut_auth2();
} // namespace auth2

#endif /* THIRD_PARTY_LIBSODIUM_UT_AUTH2_H */