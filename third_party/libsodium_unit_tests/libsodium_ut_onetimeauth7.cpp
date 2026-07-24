#include "libsodium_ut_onetimeauth7.h"

#include "unity.h"
//#include "aether/tele.h"

namespace onetimeauth7
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
        int clen;

        for (clen = 0; clen < 1000; ++clen) {
            crypto_onetimeauth_keygen(key);
            randombytes_buf(c, clen);
            crypto_onetimeauth(a, c, clen, key);
            if (crypto_onetimeauth_verify(a, c, clen, key) != 0) {
                DebugPrint("fail %d\n", clen);
                return 100;
            }
            if (clen > 0) {
                c[rand() % clen] += 1 + (rand() % 255);
                if (crypto_onetimeauth_verify(a, c, clen, key) == 0) {
                    DebugPrint("forgery %d\n", clen);
                    return 100;
                }
                a[rand() % sizeof a] += 1 + (rand() % 255);
                if (crypto_onetimeauth_verify(a, c, clen, key) == 0) {
                    DebugPrint("forgery %d\n", clen);
                    return 100;
                }
            }
        }
        return 0;
    }

    void _libsodium_ut_onetimeauth7()
    {
        test();
    }
} // namespace
