#include "libsodium_ut_box8.h"

#include "unity.h"
//#include "aether/tele.h"

namespace box8
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
        unsigned char *m;
        unsigned char *c;
        unsigned char *m2;
        size_t         mlen;
        size_t         mlen_max = 1000;
        size_t         i;
        int            faults;
        int            ret;

        m  = (unsigned char *) sodium_malloc(mlen_max);
        c  = (unsigned char *) sodium_malloc(mlen_max);
        m2 = (unsigned char *) sodium_malloc(mlen_max);
        crypto_box_keypair(alicepk, alicesk);
        crypto_box_keypair(bobpk, bobsk);
        for (mlen = 0; mlen + crypto_box_ZEROBYTES <= mlen_max; mlen++) {
            randombytes_buf(n, crypto_box_NONCEBYTES);
            randombytes_buf(m + crypto_box_ZEROBYTES, mlen);
            ret = crypto_box(c, m, mlen + crypto_box_ZEROBYTES, n, bobpk, alicesk);
            TEST_ASSERT(ret == 0);
    #ifdef BROWSER_TESTS
            faults = 1;
    #else
            faults = 5;
    #endif
            while (faults > 0) {
                c[rand() % (mlen + crypto_box_ZEROBYTES)] = rand();
                if (crypto_box_open(m2, c, mlen + crypto_box_ZEROBYTES, n, alicepk,
                                    bobsk) == 0) {
                    for (i = 0; i < mlen + crypto_box_ZEROBYTES; ++i) {
                        if (m2[i] != m[i]) {
                            DebugPrint("forgery\n");
                            return 100;
                        }
                    }
                } else {
                    faults--;
                }
            }
        }
        sodium_free(m);
        sodium_free(c);
        sodium_free(m2);

        return 0;
    }

void _libsodium_ut_box8()
    {
        test();
    }
} // namespace box8
