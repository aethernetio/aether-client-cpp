#include "libsodium_ut_core_ed25519_h2c.h"

#include "unity.h"
//#include "aether/tele.h"

namespace core_ed25519_h2c
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
        unsigned char *expected_yr, *expected_y, *y;
        char *         expected_y_hex, *y_hex;
        char *         oversized_ctx;
        size_t         i, j;
        size_t         oversized_ctx_len = 500U;

        expected_yr = (unsigned char *) sodium_malloc(crypto_core_ed25519_BYTES);
        expected_y  = (unsigned char *) sodium_malloc(crypto_core_ed25519_BYTES);
        y           = (unsigned char *) sodium_malloc(crypto_core_ed25519_BYTES);
        expected_y_hex =
            (char *) sodium_malloc(crypto_core_ed25519_BYTES * 2U + 1U);
        y_hex = (char *) sodium_malloc(crypto_core_ed25519_BYTES * 2U + 1U);
        for (i = 0U; i < (sizeof test_data) / (sizeof test_data[0]); i++) {
            sodium_hex2bin(expected_yr, crypto_core_ed25519_BYTES,
                           test_data[i].tv_y, (size_t) -1U, NULL, NULL, NULL);
            for (j = 0; j < crypto_core_ed25519_BYTES; j++) {
                expected_y[j] = expected_yr[crypto_core_ed25519_BYTES - 1U - j];
            }
            if (test_data[i].ro == 0) {
                if (crypto_core_ed25519_from_string(
                        y, "QUUX-V01-CS02-with-edwards25519_XMD:SHA-512_ELL2_NU_",
                        (const unsigned char *) test_data[i].msg,
                        strlen(test_data[i].msg), H2CHASH) != 0) {
                    DebugPrint("crypto_core_ed25519_from_string() failed\n");
                }
            } else {
                if (crypto_core_ed25519_from_string_ro(
                        y, "QUUX-V01-CS02-with-edwards25519_XMD:SHA-512_ELL2_RO_",
                        (const unsigned char *) test_data[i].msg,
                        strlen(test_data[i].msg), H2CHASH) != 0) {
                    DebugPrint("crypto_core_ed25519_from_string_ro() failed\n");
                }
            }
            y[crypto_core_ed25519_BYTES - 1U] &= 0x7f;
            if (memcmp(y, expected_y, crypto_core_ed25519_BYTES) != 0) {
                sodium_bin2hex(expected_y_hex, crypto_core_ed25519_BYTES * 2U + 1U,
                               expected_y, crypto_core_ed25519_BYTES);
                sodium_bin2hex(y_hex, crypto_core_ed25519_BYTES * 2U + 1U, y,
                               crypto_core_ed25519_BYTES);
                DebugPrint("Test #%u failed - expected [%s] got [%s]\n", (unsigned) i,
                       expected_y_hex, y_hex);
            }
        }

        if (crypto_core_ed25519_from_string(y, NULL, (const unsigned char *) "msg",
                                            3U, H2CHASH) != 0 ||
            crypto_core_ed25519_from_string(y, "", guard_page, 0U, H2CHASH) != 0 ||
            crypto_core_ed25519_from_string_ro(
                y, NULL, (const unsigned char *) "msg", 3U, H2CHASH) != 0 ||
            crypto_core_ed25519_from_string_ro(y, "", guard_page, 0U,
                                               H2CHASH) != 0) {
            DebugPrint("Failed with empty parameters");
        }

        oversized_ctx = (char *) sodium_malloc(oversized_ctx_len);
        memset(oversized_ctx, 'X', oversized_ctx_len - 1U);
        oversized_ctx[oversized_ctx_len - 1U] = 0;
        crypto_core_ed25519_from_string(y, oversized_ctx,
                                        (const unsigned char *) "msg", 3U,
                                        H2CHASH);
        sodium_bin2hex(y_hex, crypto_core_ed25519_BYTES * 2U + 1U, y,
                       crypto_core_ed25519_BYTES);
        DebugPrint("NU with oversized context: %s\n", y_hex);
        crypto_core_ed25519_from_string_ro(y, oversized_ctx,
                                           (const unsigned char *) "msg", 3U,
                                           H2CHASH);
        sodium_bin2hex(y_hex, crypto_core_ed25519_BYTES * 2U + 1U, y,
                       crypto_core_ed25519_BYTES);
        DebugPrint("RO with oversized context: %s\n", y_hex);

        sodium_free(oversized_ctx);
        sodium_free(y_hex);
        sodium_free(expected_y_hex);
        sodium_free(y);
        sodium_free(expected_y);
        sodium_free(expected_yr);

        DebugPrint("OK\n");

        return 0;
    }

    void _libsodium_ut_core_ed25519_h2c()
    {
        test();
    }
} // namespace core_ed25519
