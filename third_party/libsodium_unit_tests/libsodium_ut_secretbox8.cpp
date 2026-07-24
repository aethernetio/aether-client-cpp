#include "libsodium_ut_secretbox8.h"

#include "unity.h"
//#include "aether/tele.h"

namespace secretbox8
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
        size_t mlen;
        size_t i;
        int    caught;

        for (mlen = 0; mlen < 1000 && mlen + crypto_secretbox_ZEROBYTES < sizeof m;
             ++mlen) {
            crypto_secretbox_keygen(k);
            randombytes_buf(n, crypto_secretbox_NONCEBYTES);
            randombytes_buf(m + crypto_secretbox_ZEROBYTES, mlen);
            crypto_secretbox(c, m, mlen + crypto_secretbox_ZEROBYTES, n, k);
            caught = 0;
            while (caught < 10) {
                c[rand() % (mlen + crypto_secretbox_ZEROBYTES)] = rand();
                if (crypto_secretbox_open(m2, c, mlen + crypto_secretbox_ZEROBYTES,
                                          n, k) == 0) {
                    for (i = 0; i < mlen + crypto_secretbox_ZEROBYTES; ++i) {
                        if (m2[i] != m[i]) {
                            DebugPrint("forgery\n");
                            return 100;
                        }
                    }
                } else {
                    ++caught;
                }
            }
        }
        return 0;
    }

    void _libsodium_ut_secretbox8()
    {
        test();
    }
} // namespace
