#include "libsodium_ut_kdf.h"

#include "unity.h"
//#include "aether/tele.h"

namespace kdf
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

    static void
    tv_kdf(void)
    {
        unsigned char *master_key;
        unsigned char *subkey;
        char          *context;
        char           hex[crypto_kdf_BYTES_MAX * 2 + 1];
        uint64_t       i;
        int            ret;

        context = (char *) sodium_malloc(crypto_kdf_CONTEXTBYTES);
        master_key = (unsigned char *) sodium_malloc(crypto_kdf_KEYBYTES);

        memcpy(context, "KDF test", sizeof "KDF test" - 1U);
        for (i = 0; i < crypto_kdf_KEYBYTES; i++) {
            master_key[i] = i;
        }
        subkey = (unsigned char *) sodium_malloc(crypto_kdf_BYTES_MAX);
        for (i = 0; i < 10; i++) {
            ret = crypto_kdf_derive_from_key(subkey, crypto_kdf_BYTES_MAX,
                                             i, context, master_key);
            TEST_ASSERT(ret == 0);
            sodium_bin2hex(hex, sizeof hex, subkey, crypto_kdf_BYTES_MAX);
            DebugPrint("%s\n", hex);
        }
        sodium_free(subkey);

        for (i = 0; i < crypto_kdf_BYTES_MAX + 2; i++) {
            subkey = (unsigned char *) sodium_malloc(crypto_kdf_BYTES_MAX);
            if (crypto_kdf_derive_from_key(subkey, (size_t) i,
                                           i, context, master_key) == 0) {
                sodium_bin2hex(hex, sizeof hex, subkey, (size_t) i);
                DebugPrint("%s\n", hex);
            } else {
                DebugPrint("Failure -- probably expected for output length=%u\n",
                       (unsigned int) i);
            }
            sodium_free(subkey);
        }

        sodium_free(master_key);
        sodium_free(context);

        TEST_ASSERT(strcmp(crypto_kdf_primitive(), crypto_kdf_PRIMITIVE) == 0);
        TEST_ASSERT(crypto_kdf_BYTES_MAX > 0);
        TEST_ASSERT(crypto_kdf_BYTES_MIN <= crypto_kdf_BYTES_MAX);
        TEST_ASSERT(crypto_kdf_bytes_min() == crypto_kdf_BYTES_MIN);
        TEST_ASSERT(crypto_kdf_bytes_max() == crypto_kdf_BYTES_MAX);
        TEST_ASSERT(crypto_kdf_CONTEXTBYTES > 0);
        TEST_ASSERT(crypto_kdf_contextbytes() == crypto_kdf_CONTEXTBYTES);
        TEST_ASSERT(crypto_kdf_KEYBYTES >= 16);
        TEST_ASSERT(crypto_kdf_keybytes() == crypto_kdf_KEYBYTES);
        TEST_ASSERT(crypto_kdf_bytes_min() == crypto_kdf_blake2b_bytes_min());
        TEST_ASSERT(crypto_kdf_bytes_max() == crypto_kdf_blake2b_bytes_max());
        TEST_ASSERT(crypto_kdf_contextbytes() == crypto_kdf_blake2b_contextbytes());
        TEST_ASSERT(crypto_kdf_keybytes() == crypto_kdf_blake2b_keybytes());

        DebugPrint("tv_kdf: ok\n");
    }

    int
    test(void)
    {
        tv_kdf();

        return 0;
    }

    void _libsodium_ut_kdf()
    {
        test();
    }
} // namespace
