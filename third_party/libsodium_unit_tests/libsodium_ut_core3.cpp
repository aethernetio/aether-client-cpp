#include "libsodium_ut_core3.h"

#include "unity.h"
//#include "aether/tele.h"

namespace core3
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
        unsigned char *secondkey;
        unsigned char *c;
        unsigned char *noncesuffix;
        unsigned char *in;
        unsigned char *output;
        unsigned char *h;
        size_t         output_len = 64 * 256 * 256;
        size_t         pos = 0;
        int            i;

        pos = 0;
        secondkey = (unsigned char *) sodium_malloc(32);
        memcpy(secondkey, SECONDKEY, 32);
        noncesuffix = (unsigned char *) sodium_malloc(8);
        memcpy(noncesuffix, NONCESUFFIX, 8);
        c = (unsigned char *) sodium_malloc(16);
        memcpy(c, C, 16);
        in = (unsigned char *) sodium_malloc(16);
        output = (unsigned char *) sodium_malloc(output_len);
        h = (unsigned char *) sodium_malloc(32);

        for (i = 0; i < 8; i++) {
            in[i] = noncesuffix[i];
        }
        for (; i < 16; i++) {
            in[i] = 0;
        }
        do {
            do {
                crypto_core_salsa20(output + pos, in, secondkey, c);
                pos += 64;
                in[8]++;
            } while (in[8] != 0);
            in[9]++;
        } while (in[9] != 0);

        crypto_hash_sha256(h, output, output_len);

        for (i = 0; i < 32; ++i) {
            DebugPrint("%02x", h[i]);
        }
        DebugPrint("\n");

    #ifndef SODIUM_LIBRARY_MINIMAL
        pos = 0;
        do {
            do {
                crypto_core_salsa2012(output + pos, in, secondkey, c);
                pos += 64;
                in[8]++;
            } while (in[8] != 0);
            in[9]++;
        } while (in[9] != 0);

        crypto_hash_sha256(h, output, output_len);

        for (i = 0; i < 32; ++i) {
            DebugPrint("%02x", h[i]);
        }
        DebugPrint("\n");

        pos = 0;
        do {
            do {
                crypto_core_salsa208(output + pos, in, secondkey, c);
                pos += 64;
                in[8]++;
            } while (in[8] != 0);
            in[9]++;
        } while (in[9] != 0);

        crypto_hash_sha256(h, output, output_len);

        for (i = 0; i < 32; ++i) {
            DebugPrint("%02x", h[i]);
        }
        DebugPrint("\n");
    #else
        DebugPrint("a4e3147dddd2ba7775939b50208a22eb3277d4e4bad8a1cfbc999c6bd392b638\n"
               "017421baa9959cbe894bd003ec87938254f47c1e757eb66cf89c353d0c2b68de\n");
    #endif

        sodium_free(h);
        sodium_free(output);
        sodium_free(in);
        sodium_free(c);
        sodium_free(noncesuffix);
        sodium_free(secondkey);

        TEST_ASSERT(crypto_core_salsa20_outputbytes() == crypto_core_salsa20_OUTPUTBYTES);
        TEST_ASSERT(crypto_core_salsa20_inputbytes() == crypto_core_salsa20_INPUTBYTES);
        TEST_ASSERT(crypto_core_salsa20_keybytes() == crypto_core_salsa20_KEYBYTES);
        TEST_ASSERT(crypto_core_salsa20_constbytes() == crypto_core_salsa20_CONSTBYTES);

        return 0;
    }

    void _libsodium_ut_core3()
    {
        test();
    }
} // namespace core3
