#include "libsodium_ut_sign2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace sign2
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
        crypto_sign_state st;
        unsigned char     m[1024];
        unsigned char     sig[crypto_sign_BYTES];
        unsigned char     pk[crypto_sign_PUBLICKEYBYTES];
        char              m_hex[1024 * 2 + 1];
        char              sig_hex[crypto_sign_BYTES * 2 + 1];
        char              pk_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
        size_t            m_len;
        unsigned int      i;
        int               ret;

        for (i = 0U; i < (sizeof test_data) / (sizeof test_data[0]); i++) {
            sodium_hex2bin(m, sizeof m, test_data[i].m, strlen(test_data[i].m), NULL, &m_len, NULL);
            sodium_hex2bin(pk, sizeof pk, test_data[i].pk, strlen(test_data[i].pk), NULL, NULL, NULL);
            sodium_hex2bin(sig, sizeof sig, test_data[i].sig, strlen(test_data[i].sig), NULL, NULL,
                           NULL);
            ret = crypto_sign_verify_detached(sig, m, m_len, pk);
            if (test_data[i].ret != ret) {
                DebugPrint("Test %u -- expected: %d found: %d\n", (unsigned int) i, test_data[i].ret, ret);
            }
        }
        DebugPrint("OK\n");

        return 0;
    }

    void _libsodium_ut_sign2()
    {
        test();
    }
} // namespace
