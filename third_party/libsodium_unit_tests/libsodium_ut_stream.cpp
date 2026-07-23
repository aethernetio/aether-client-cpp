#include "libsodium_ut_stream.h"

#include "unity.h"
//#include "aether/tele.h"

namespace stream
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

    int
    test(void)
    {
        unsigned char  h[32];
        char          *hex;
        unsigned char *output;
        size_t         sizeof_hex = 17 * 64 * 2 + 1;
        size_t         sizeof_output = 4194304;
        int            i;

        output = (unsigned char *) sodium_malloc(sizeof_output);
        hex = (char *) sodium_malloc(sizeof_hex);

        randombytes_buf(output, sizeof_output);
        crypto_stream(output, sizeof_output, nonce, firstkey);
        crypto_hash_sha256(h, output, sizeof_output);
        sodium_bin2hex(hex, sizeof_hex, h, sizeof h);
        DebugPrint("%s\n", hex);

        TEST_ASSERT(sizeof_output > 4000);

        crypto_stream_xsalsa20_xor_ic(output, output, 4000, nonce, 0U, firstkey);
        for (i = 0; i < 4000; i++) {
            TEST_ASSERT(output[i] == 0);
        }
        crypto_stream_xsalsa20_xor_ic(output, output, 4000, nonce, 1U, firstkey);
        crypto_hash_sha256(h, output, sizeof_output);
        sodium_bin2hex(hex, sizeof_hex, h, sizeof h);
        DebugPrint("%s\n", hex);

        for (i = 0; i < 64; i++) {
            memset(output, i, 64);
            crypto_stream(output, (int) (i & 0xff), nonce, firstkey);
            sodium_bin2hex(hex, sizeof_hex, output, 64);
            DebugPrint("%s\n", hex);
        }

        memset(output, 0, 192);
        crypto_stream_xsalsa20_xor_ic(output, output, 192, nonce,
                                      (1ULL << 32) - 1ULL, firstkey);
        sodium_bin2hex(hex, 192 * 2 + 1, output, 192);
        DebugPrint("%s\n", hex);

        for (i = 16; i > 0; i--) {
            memset(output, 0, 17 * 64);
            crypto_stream_xsalsa20_xor_ic(output, output, 17 * 64, nonce,
                                          (1ULL << 32) - (unsigned long long) i,
                                          firstkey);
            sodium_bin2hex(hex, 2 * 17 * 64 + 1, output, 17 * 64);
            DebugPrint("%s\n", hex);
        }

        sodium_free(hex);
        sodium_free(output);

        TEST_ASSERT(crypto_stream_keybytes() > 0U);
        TEST_ASSERT(crypto_stream_noncebytes() > 0U);
        TEST_ASSERT(crypto_stream_messagebytes_max() > 0U);
        TEST_ASSERT(strcmp(crypto_stream_primitive(), "xsalsa20") == 0);
        TEST_ASSERT(crypto_stream_keybytes() == crypto_stream_xsalsa20_keybytes());
        TEST_ASSERT(crypto_stream_noncebytes() == crypto_stream_xsalsa20_noncebytes());
        TEST_ASSERT(crypto_stream_messagebytes_max() == crypto_stream_xsalsa20_messagebytes_max());

        return 0;
    }

    void _libsodium_ut_stream()
    {
        test();
    }
} // namespace
