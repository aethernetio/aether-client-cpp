#include "libsodium_ut_hash.h"

#include "unity.h"
//#include "aether/tele.h"

namespace hash
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
        size_t i;

        crypto_hash(h, x, sizeof x - 1U);
        for (i = 0; i < crypto_hash_BYTES; ++i) {
            DebugPrint("%02x", (unsigned int) h[i]);
        }
        DebugPrint("\n");
        crypto_hash(h, x2, sizeof x2 - 1U);
        for (i = 0; i < crypto_hash_BYTES; ++i) {
            DebugPrint("%02x", (unsigned int) h[i]);
        }
        DebugPrint("\n");
        crypto_hash_sha256(h, x, sizeof x - 1U);
        for (i = 0; i < crypto_hash_sha256_BYTES; ++i) {
            DebugPrint("%02x", (unsigned int) h[i]);
        }
        DebugPrint("\n");
        crypto_hash_sha256(h, x2, sizeof x2 - 1U);
        for (i = 0; i < crypto_hash_sha256_BYTES; ++i) {
            DebugPrint("%02x", (unsigned int) h[i]);
        }
        DebugPrint("\n");

        TEST_ASSERT(crypto_hash_bytes() > 0U);
        TEST_ASSERT(strcmp(crypto_hash_primitive(), "sha512") == 0);
        TEST_ASSERT(crypto_hash_sha256_bytes() > 0U);
        TEST_ASSERT(crypto_hash_sha512_bytes() >= crypto_hash_sha256_bytes());
        TEST_ASSERT(crypto_hash_sha512_bytes() == crypto_hash_bytes());
        TEST_ASSERT(crypto_hash_sha256_statebytes() == sizeof(crypto_hash_sha256_state));
        TEST_ASSERT(crypto_hash_sha512_statebytes() == sizeof(crypto_hash_sha512_state));

        return 0;
    }

    void _libsodium_ut_hash()
    {
        test();
    }
} // namespace
