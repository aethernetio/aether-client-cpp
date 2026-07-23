#include "libsodium_ut_generichash2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace generichash2
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
    #define MAXLEN 64
        crypto_generichash_state *st;
        unsigned char            in[MAXLEN];
        unsigned char            out[crypto_generichash_BYTES_MAX];
        unsigned char            k[crypto_generichash_KEYBYTES_MAX];
        size_t                   h, i, j;

        TEST_ASSERT(crypto_generichash_statebytes() >= sizeof *st);
        st = (crypto_generichash_state *)
            sodium_malloc(crypto_generichash_statebytes());
        for (h = 0; h < crypto_generichash_KEYBYTES_MAX; ++h) {
            k[h] = (unsigned char) h;
        }
        for (i = 0; i < MAXLEN; ++i) {
            in[i] = (unsigned char) i;
            if (crypto_generichash_init(st, k,
                                        1 + i % crypto_generichash_KEYBYTES_MAX,
                                        1 + i % crypto_generichash_BYTES_MAX) != 0) {
                DebugPrint("crypto_generichash_init()\n");
                return 1;
            }
            crypto_generichash_update(st, in, i);
            crypto_generichash_update(st, in, i);
            crypto_generichash_update(st, in, i);
            if (crypto_generichash_final(st, out,
                                         1 + i % crypto_generichash_BYTES_MAX) != 0) {
                DebugPrint("crypto_generichash_final() should have returned 0\n");
            }
            for (j = 0; j < 1 + i % crypto_generichash_BYTES_MAX; ++j) {
                DebugPrint("%02x", (unsigned int) out[j]);
            }
            DebugPrint("\n");
            if (crypto_generichash_final(st, out,
                                         1 + i % crypto_generichash_BYTES_MAX) != -1) {
                DebugPrint("crypto_generichash_final() should have returned -1\n");
            }
        }

        TEST_ASSERT(crypto_generichash_init(st, k, sizeof k, 0U) == -1);
        TEST_ASSERT(crypto_generichash_init(st, k, sizeof k,
                                       crypto_generichash_BYTES_MAX + 1U) == -1);
        TEST_ASSERT(crypto_generichash_init(st, k, crypto_generichash_KEYBYTES_MAX + 1U,
                                       sizeof out) == -1);
        TEST_ASSERT(crypto_generichash_init(st, k, 0U, sizeof out) == 0);
        TEST_ASSERT(crypto_generichash_init(st, k, 1U, sizeof out) == 0);
        TEST_ASSERT(crypto_generichash_init(st, NULL, 1U, 0U) == -1);
        TEST_ASSERT(crypto_generichash_init(st, NULL, crypto_generichash_KEYBYTES,
                                       1U) == 0);
        TEST_ASSERT(crypto_generichash_init(st, NULL, crypto_generichash_KEYBYTES,
                                       0U) == -1);

        sodium_free(st);

        return 0;
    }

    void _libsodium_ut_generichash2()
    {
        test();
    }
} // namespace
