#include "libsodium_ut_generichash.h"

#include "unity.h"
//#include "aether/tele.h"

namespace generichash
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
    tv(void)
    {
        unsigned char *expected_out;
        unsigned char *in;
        unsigned char *key;
        unsigned char *out;
        size_t         i = 0U;
        size_t         in_len;

        key = (unsigned char *) sodium_malloc(crypto_generichash_KEYBYTES_MAX);
        out = (unsigned char *) sodium_malloc(crypto_generichash_BYTES_MAX);
        expected_out = (unsigned char *) sodium_malloc(crypto_generichash_BYTES_MAX);
        do {
            TEST_ASSERT(strlen(tests[i].key_hex) == 2 * crypto_generichash_KEYBYTES_MAX);
            sodium_hex2bin(key, crypto_generichash_KEYBYTES_MAX,
                           tests[i].key_hex, strlen(tests[i].key_hex),
                           NULL, NULL, NULL);
            TEST_ASSERT(strlen(tests[i].out_hex) == 2 * crypto_generichash_BYTES_MAX);
            sodium_hex2bin(expected_out, crypto_generichash_BYTES_MAX,
                           tests[i].out_hex, strlen(tests[i].out_hex),
                           NULL, NULL, NULL);
            in_len = strlen(tests[i].in_hex) / 2;
            in = (unsigned char *) sodium_malloc(in_len);
            sodium_hex2bin(in, in_len, tests[i].in_hex, strlen(tests[i].in_hex),
                           NULL, NULL, NULL);
            crypto_generichash(out, crypto_generichash_BYTES_MAX,
                               in, (unsigned long long) in_len,
                               key, crypto_generichash_KEYBYTES_MAX);
            if (memcmp(out, expected_out, crypto_generichash_BYTES_MAX) != 0) {
                DebugPrint("Test vector #%u failed\n", (unsigned int) i);
            }
            sodium_free(in);
        } while (++i < (sizeof tests) / (sizeof tests[0]));
        sodium_free(key);
        sodium_free(out);
        sodium_free(expected_out);

        return 0;
    }

    int
    test(void)
    {
        unsigned char in[MAXLEN];
        unsigned char out[crypto_generichash_BYTES_MAX];
        unsigned char k[crypto_generichash_KEYBYTES_MAX];
        size_t        h;
        size_t        i;
        size_t        j;

        tv();

        for (h = 0; h < crypto_generichash_KEYBYTES_MAX; ++h) {
            k[h] = (unsigned char) h;
        }

        for (i = 0; i < MAXLEN; ++i) {
            in[i] = (unsigned char) i;
            crypto_generichash(out, 1 + i % crypto_generichash_BYTES_MAX, in,
                               (unsigned long long) i, k,
                               1 + i % crypto_generichash_KEYBYTES_MAX);
            for (j = 0; j < 1 + i % crypto_generichash_BYTES_MAX; ++j) {
                DebugPrint("%02x", (unsigned int) out[j]);
            }
            DebugPrint("\n");
        }

        memset(out, 0, sizeof out);
        crypto_generichash(out, crypto_generichash_BYTES_MAX, in,
                           (unsigned long long) i, k, 0U);
        for (j = 0; j < crypto_generichash_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        TEST_ASSERT(crypto_generichash(guard_page, 0,
                                  in, (unsigned long long) sizeof in,
                                  k, sizeof k) == -1);
        TEST_ASSERT(crypto_generichash(guard_page, crypto_generichash_BYTES_MAX + 1,
                                  in, (unsigned long long) sizeof in,
                                  k, sizeof k) == -1);
        TEST_ASSERT(crypto_generichash(guard_page, (unsigned long long) sizeof in,
                                  in, (unsigned long long) sizeof in,
                                  k, crypto_generichash_KEYBYTES_MAX + 1) == -1);

        TEST_ASSERT(crypto_generichash_bytes_min() > 0U);
        TEST_ASSERT(crypto_generichash_bytes_max() > 0U);
        TEST_ASSERT(crypto_generichash_bytes() > 0U);
        TEST_ASSERT(crypto_generichash_bytes() >= crypto_generichash_bytes_min());
        TEST_ASSERT(crypto_generichash_bytes() <= crypto_generichash_bytes_max());
        TEST_ASSERT(crypto_generichash_keybytes_min() > 0U);
        TEST_ASSERT(crypto_generichash_keybytes_max() > 0U);
        TEST_ASSERT(crypto_generichash_keybytes() > 0U);
        TEST_ASSERT(crypto_generichash_keybytes() >= crypto_generichash_keybytes_min());
        TEST_ASSERT(crypto_generichash_keybytes() <= crypto_generichash_keybytes_max());
        TEST_ASSERT(strcmp(crypto_generichash_primitive(), "blake2b") == 0);
        TEST_ASSERT(crypto_generichash_bytes_min()
               == crypto_generichash_blake2b_bytes_min());
        TEST_ASSERT(crypto_generichash_bytes_max()
               == crypto_generichash_blake2b_bytes_max());
        TEST_ASSERT(crypto_generichash_bytes() == crypto_generichash_blake2b_bytes());
        TEST_ASSERT(crypto_generichash_keybytes_min()
               == crypto_generichash_blake2b_keybytes_min());
        TEST_ASSERT(crypto_generichash_keybytes_max()
               == crypto_generichash_blake2b_keybytes_max());
        TEST_ASSERT(crypto_generichash_keybytes()
               == crypto_generichash_blake2b_keybytes());
        TEST_ASSERT(crypto_generichash_blake2b_saltbytes() > 0U);
        TEST_ASSERT(crypto_generichash_blake2b_personalbytes() > 0U);

        return 0;
    }

    void _libsodium_ut_generichash()
    {
        test();
    }
} // namespace
