#include "libsodium_ut_shorthash.h"

#include "unity.h"
//#include "aether/tele.h"

namespace shorthash
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
        unsigned char in[MAXLEN];
        unsigned char out[crypto_shorthash_BYTES];
        unsigned char k[crypto_shorthash_KEYBYTES];
        size_t        i;
        size_t        j;

        for (i = 0; i < crypto_shorthash_KEYBYTES; ++i) {
            k[i] = (unsigned char) i;
        }
        for (i = 0; i < MAXLEN; ++i) {
            in[i] = (unsigned char) i;
            crypto_shorthash(out, in, (unsigned long long) i, k);
            for (j = 0; j < crypto_shorthash_BYTES; ++j) {
                DebugPrint("%02x", (unsigned int) out[j]);
            }
            DebugPrint("\n");
        }
        TEST_ASSERT(crypto_shorthash_bytes() > 0);
        TEST_ASSERT(crypto_shorthash_keybytes() > 0);
        TEST_ASSERT(strcmp(crypto_shorthash_primitive(), "siphash24") == 0);
        TEST_ASSERT(crypto_shorthash_bytes() == crypto_shorthash_siphash24_bytes());
        TEST_ASSERT(crypto_shorthash_keybytes() ==
               crypto_shorthash_siphash24_keybytes());

        return 0;
    }

    void _libsodium_ut_shorthash()
    {
        test();
    }
} // namespace
