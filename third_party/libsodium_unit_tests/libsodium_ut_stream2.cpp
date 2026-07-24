#include "libsodium_ut_stream2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace stream2
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
        unsigned char *output;
        char          *hex;
        unsigned char  h[32];
        size_t         sizeof_hex = 32 * 2 + 1;
        size_t         sizeof_output = 4194304;
        int            i;

        output = (unsigned char *) sodium_malloc(sizeof_output);
        hex = (char *) sodium_malloc(sizeof_hex);

        crypto_stream_salsa20(output, sizeof_output, noncesuffix, secondkey);
        crypto_hash_sha256(h, output, sizeof_output);
        sodium_bin2hex(hex, sizeof_hex, h, sizeof h);
        DebugPrint("%s\n", hex);

        TEST_ASSERT(sizeof_output > 4000);

        crypto_stream_salsa20_xor_ic(output, output, 4000, noncesuffix, 0U,
                                     secondkey);
        for (i = 0; i < 4000; i++) {
            TEST_ASSERT(output[i] == 0);
        }

        crypto_stream_salsa20_xor_ic(output, output, 4000, noncesuffix, 1U,
                                     secondkey);
        crypto_hash_sha256(h, output, sizeof_output);
        sodium_bin2hex(hex, sizeof_hex, h, sizeof h);
        DebugPrint("%s\n", hex);

        sodium_free(hex);
        sodium_free(output);

        TEST_ASSERT(crypto_stream_salsa20_keybytes() > 0U);
        TEST_ASSERT(crypto_stream_salsa20_noncebytes() > 0U);
        TEST_ASSERT(crypto_stream_salsa20_messagebytes_max() > 0U);

        return 0;
    }


    void _libsodium_ut_stream2()
    {
        test();
    }
} // namespace
