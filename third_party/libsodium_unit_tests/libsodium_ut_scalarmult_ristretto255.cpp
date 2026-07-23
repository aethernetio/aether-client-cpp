#include "libsodium_ut_scalarmult_ristretto255.h"

#include "unity.h"
//#include "aether/tele.h"

namespace scalarmult_ristretto255
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
        unsigned char *b =
            (unsigned char *) sodium_malloc(crypto_scalarmult_ristretto255_BYTES);
        unsigned char *n =
            (unsigned char *) sodium_malloc(crypto_scalarmult_ristretto255_SCALARBYTES);
        unsigned char *p =
            (unsigned char *) sodium_malloc(crypto_scalarmult_ristretto255_BYTES);
        unsigned char *p2 =
            (unsigned char *) sodium_malloc(crypto_scalarmult_ristretto255_BYTES);
        char          *hex =
            (char *) sodium_malloc(2 * crypto_scalarmult_ristretto255_BYTES + 1);
        int            i;

        sodium_hex2bin(b, crypto_scalarmult_ristretto255_BYTES,
                       B_HEX, sizeof B_HEX - (size_t) 1U, NULL, NULL, NULL);
        memset(n, 0, crypto_scalarmult_ristretto255_SCALARBYTES);
        for (i = 0; i < 16; i++) {
            crypto_scalarmult_ristretto255_base(p, n);
            if (crypto_scalarmult_ristretto255(p2, n, b) != 0) {
                DebugPrint("crypto_scalarmult_ristretto255(%d) != 0\n", i);
            }
            sodium_bin2hex(hex, 2 * crypto_scalarmult_ristretto255_BYTES + 1,
                           p, crypto_scalarmult_ristretto255_BYTES);
            DebugPrint("%s\n", hex);
            TEST_ASSERT(memcmp(p, p2, crypto_scalarmult_ristretto255_BYTES) == 0);
            sodium_increment(n, crypto_scalarmult_ristretto255_SCALARBYTES);
        }

        memset(p, 0xfe, crypto_scalarmult_ristretto255_BYTES);
        TEST_ASSERT(crypto_scalarmult_ristretto255(guard_page, n, p) == -1);

        sodium_free(hex);
        sodium_free(p2);
        sodium_free(p);
        sodium_free(n);
        sodium_free(b);

        TEST_ASSERT(crypto_scalarmult_ristretto255_BYTES == crypto_scalarmult_ristretto255_bytes());
        TEST_ASSERT(crypto_scalarmult_ristretto255_SCALARBYTES == crypto_scalarmult_ristretto255_scalarbytes());

        DebugPrint("OK\n");

        return 0;
    }

    void _libsodium_ut_scalarmult_ristretto255()
    {
        test();
    }
} // namespace
