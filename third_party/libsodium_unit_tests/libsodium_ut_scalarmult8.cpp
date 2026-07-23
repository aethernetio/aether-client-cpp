#include "libsodium_ut_scalarmult8.h"

#include "unity.h"
//#include "aether/tele.h"

namespace scalarmult8
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
        unsigned char sk[crypto_scalarmult_SCALARBYTES];
        unsigned char pk[crypto_scalarmult_BYTES];
        unsigned char shared[crypto_scalarmult_BYTES];
        unsigned char shared2[crypto_scalarmult_BYTES];
        unsigned int  i;
        int           res;

        for (i = 0U; i < (sizeof test_data) / (sizeof test_data[0]); i++) {
            sodium_hex2bin(sk, crypto_scalarmult_SCALARBYTES, test_data[i].sk_hex,
                           crypto_scalarmult_SCALARBYTES * 2, NULL, NULL, NULL);
            sodium_hex2bin(pk, crypto_scalarmult_BYTES, test_data[i].pk_hex,
                           crypto_scalarmult_BYTES * 2, NULL, NULL, NULL);
            sodium_hex2bin(shared, crypto_scalarmult_BYTES, test_data[i].shared_hex,
                           crypto_scalarmult_BYTES * 2, NULL, NULL, NULL);
            randombytes_buf(shared2, crypto_scalarmult_BYTES);
            res = crypto_scalarmult(shared2, sk, pk);
            if (res == 0) {
                if (strcmp(test_data[i].outcome, "acceptable") == 0) {
                    DebugPrint("test case %u succeeded (%s)\n", i,
                           test_data[i].outcome);
                } else if (strcmp(test_data[i].outcome, "valid") != 0) {
                    DebugPrint("*** test case %u succeeded, was supposed to be %s\n", i,
                           test_data[i].outcome);
                }
                if (memcmp(shared, shared2, crypto_scalarmult_BYTES) != 0) {
                    DebugPrint("*** test case %u succeeded, but shared key is not %s\n",
                           i, test_data[i].outcome);
                }
            } else {
                if (strcmp(test_data[i].outcome, "acceptable") == 0) {
                    DebugPrint("test case %u failed (%s)\n", i, test_data[i].outcome);
                } else if (strcmp(test_data[i].outcome, "valid") == 0) {
                    DebugPrint("*** test case %u failed, was supposed to be %s\n", i,
                           test_data[i].outcome);
                }
            }
        }
        DebugPrint("OK\n");

        return 0;
    }

    void _libsodium_ut_scalarmult8()
    {
        test();
    }
} // namespace
