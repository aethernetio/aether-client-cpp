#include "libsodium_ut_randombytes.h"

#include "unity.h"
//#include "aether/tele.h"

namespace randombytes_sodium
{
    #define DEBUG_OUT 1
    #define LOG(TAG, ...) {\
     fprintf(stderr, "file location%s\nfunction name:%s\nline:%d\n", __FILE__, __FUNCTION__, __LINE__);\
     fprintf(stderr, "date:%s\ntime:%s\n",__DATE__, __TIME__);\
     fprintf(stderr, "%s", TAG);\
     fprintf(stderr, "\r\n");\
     fprintf(stderr, __VA_ARGS__);\
     fprintf(stderr, "\n");\
    }

    static const char *TAG = "SODIUM UNITY";

    #if DEBUG_OUT==1
        #define DebugPrint(format, ...) LOG(TAG, format, ##__VA_ARGS__)
    #elif DEBUG_OUT==2
        #define DebugPrint(format, ...) AE_TELED_DEBUG(TAG, format, ##__VA_ARGS__)
    #elif DEBUG_OUT==3
        #define DebugPrint(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
    #endif

    static int
    compat_tests(void)
    {
        size_t i;

        memset(x, 0, sizeof x);
        randombytes(x, sizeof x);
        for (i = 0; i < 256; ++i) {
            freq[i] = 0;
        }
        for (i = 0; i < sizeof x; ++i) {
            ++freq[255 & (int) x[i]];
        }
        for (i = 0; i < 256; ++i) {
            if (!freq[i]) {
                DebugPrint("nacl_tests failed\n");
            }
        }
        return 0;
    }

    static int
    randombytes_tests(void)
    {
        static const unsigned char seed[randombytes_SEEDBYTES] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
            0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
            0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
        };
        unsigned char out[100];
        unsigned int  f = 0U;
        unsigned int  i;
        uint32_t      n;

    #ifndef BENCHMARKS
    # ifdef __EMSCRIPTEN__
        TEST_ASSERT(strcmp(randombytes_implementation_name(), "js") == 0);
    # else
        TEST_ASSERT(strcmp(randombytes_implementation_name(), "sysrandom") == 0);
    # endif
    #endif
        randombytes(x, 1U);
        do {
            n = randombytes_random();
            f |= ((n >> 24) > 1);
            f |= ((n >> 16) > 1) << 1;
            f |= ((n >> 8) > 1) << 2;
            f |= ((n) > 1) << 3;
            f |= (n > 0x7fffffff) << 4;
        } while (f != 0x1f);
        randombytes_close();

        for (i = 0; i < 256; ++i) {
            freq[i] = 0;
        }
        for (i = 0; i < 65536; ++i) {
            ++freq[randombytes_uniform(256)];
        }
        for (i = 0; i < 256; ++i) {
            if (!freq[i]) {
                DebugPrint("randombytes_uniform() test failed\n");
            }
        }
        TEST_ASSERT(randombytes_uniform(1U) == 0U);
        randombytes_close();
    #ifndef __EMSCRIPTEN__
        TEST_ASSERT(&randombytes_internal_implementation == &randombytes_salsa20_implementation);
        randombytes_set_implementation(&randombytes_internal_implementation);
        TEST_ASSERT(strcmp(randombytes_implementation_name(), "internal") == 0);
    #endif
        randombytes_stir();
        for (i = 0; i < 256; ++i) {
            freq[i] = 0;
        }
        for (i = 0; i < 65536; ++i) {
            ++freq[randombytes_uniform(256)];
        }
        for (i = 0; i < 256; ++i) {
            if (!freq[i]) {
                DebugPrint("randombytes_uniform() test failed\n");
            }
        }
        memset(x, 0, sizeof x);
        randombytes_buf(x, sizeof x);
        for (i = 0; i < 256; ++i) {
            freq[i] = 0;
        }
        for (i = 0; i < sizeof x; ++i) {
            ++freq[255 & (int) x[i]];
        }
        for (i = 0; i < 256; ++i) {
            if (!freq[i]) {
                DebugPrint("randombytes_buf() test failed\n");
            }
        }
        TEST_ASSERT(randombytes_uniform(1U) == 0U);

        randombytes_buf_deterministic(out, sizeof out, seed);
        for (i = 0; i < sizeof out; ++i) {
            DebugPrint("%02x", out[i]);
        }
        DebugPrint(" (deterministic)\n");

        randombytes_close();

        randombytes(x, 1U);
        randombytes_close();

        TEST_ASSERT(randombytes_SEEDBYTES > 0);
        TEST_ASSERT(randombytes_seedbytes() == randombytes_SEEDBYTES);

        return 0;
    }

    static uint32_t
    randombytes_uniform_impl(const uint32_t upper_bound)
    {
        return upper_bound;
    }

    static int
    impl_tests(void)
    {
        randombytes_implementation impl = randombytes_sysrandom_implementation;
        uint32_t                   v = randombytes_random();

        impl.uniform = randombytes_uniform_impl;
        randombytes_close();
        randombytes_set_implementation(&impl);
        TEST_ASSERT(randombytes_uniform(1) == 1);
        TEST_ASSERT(randombytes_uniform(v) == v);
        TEST_ASSERT(randombytes_uniform(v) == v);
        TEST_ASSERT(randombytes_uniform(v) == v);
        TEST_ASSERT(randombytes_uniform(v) == v);
        randombytes_close();
        impl.close = NULL;
        randombytes_close();

        return 0;
    }

    int
    test(void)
    {
        compat_tests();
        randombytes_tests();
    #ifndef __EMSCRIPTEN__
        impl_tests();
    #endif
        DebugPrint("OK\n");

    #ifndef __EMSCRIPTEN__
        randombytes_set_implementation(&randombytes_salsa20_implementation);
    #endif

        return 0;
    }

    void _libsodium_ut_randombytes()
    {
        test();
    }
} // namespace
