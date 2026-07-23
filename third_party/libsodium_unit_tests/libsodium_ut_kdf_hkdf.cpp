#include "libsodium_ut_kdf_hkdf.h"

#include "unity.h"
//#include "aether/tele.h"

namespace kdf_hkdf
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
    tv_kdf_hkdf(void)
    {
        unsigned char *master_key;
        size_t         master_key_len = 66;
        unsigned char *prk256;
        size_t         prk256_len = crypto_kdf_hkdf_sha256_KEYBYTES;
        unsigned char *prk512;
        size_t         prk512_len = crypto_kdf_hkdf_sha512_KEYBYTES;
        unsigned char *salt;
        size_t         salt_len = 77;
        char          *context;
        size_t         context_len = 88;
        unsigned char *out;
        size_t         out_len = 99;
        char           hex[99 * 2 + 1];
        size_t         i;
        int            ret;

        master_key = (unsigned char *) sodium_malloc(master_key_len);
        prk256 = (unsigned char *) sodium_malloc(prk256_len);
        prk512 = (unsigned char *) sodium_malloc(prk512_len);
        salt = (unsigned char *) sodium_malloc(salt_len);
        context = (char *) sodium_malloc(context_len);
        out = (unsigned char *) sodium_malloc(out_len);
        for (i = 0; i < master_key_len; i++) {
            master_key[i] = i;
        }
        for (i = 0; i < salt_len; i++) {
            salt[i] = (unsigned char) ~i;
        }
        for (i = 0; i < context_len; i++) {
            context[i] = (unsigned char) (i + 111);
        }

        DebugPrint("\nHKDF/SHA-256:\n");
        crypto_kdf_hkdf_sha256_keygen(prk256);
        if (crypto_kdf_hkdf_sha256_extract(prk256, salt, salt_len,
                                           master_key, master_key_len) != 0) {
            DebugPrint("hkdf_sha256_extract() failed\n");
        }
        DebugPrint("PRK: %s\n", sodium_bin2hex(hex, sizeof hex, prk256, prk256_len));

        for (i = 0; i < out_len; i++) {
            context[0] = i;
            if (crypto_kdf_hkdf_sha256_expand(out, i,
                                              context, context_len, prk256) != 0) {
                DebugPrint("hkdf_sha256_expand() failed\n");
            }
            DebugPrint("%s\n", sodium_bin2hex(hex, sizeof hex, out, i));
        }

        DebugPrint("\nHKDF/SHA-512:\n");
        crypto_kdf_hkdf_sha256_keygen(prk512);
        if (crypto_kdf_hkdf_sha512_extract(prk512, salt, salt_len,
                                           master_key, master_key_len) != 0) {
            DebugPrint("hkdf_sha512_extract() failed\n");
        }
        DebugPrint("PRK: %s\n", sodium_bin2hex(hex, sizeof hex, prk512, prk512_len));

        for (i = 0; i < out_len; i++) {
            context[0] = i;
            if (crypto_kdf_hkdf_sha512_expand(out, i,
                                              context, context_len, prk512) != 0) {
                DebugPrint("hkdf_sha512_expand() failed\n");
            }
            DebugPrint("%s\n", sodium_bin2hex(hex, sizeof hex, out, i));
        }

        sodium_free(out);
        sodium_free(context);
        sodium_free(salt);
        sodium_free(master_key);
        sodium_free(prk512);
        sodium_free(prk256);

        TEST_ASSERT(crypto_kdf_hkdf_sha512_bytes_min() == crypto_kdf_hkdf_sha512_BYTES_MIN);
        TEST_ASSERT(crypto_kdf_hkdf_sha512_bytes_max() == crypto_kdf_hkdf_sha512_BYTES_MAX);
        TEST_ASSERT(crypto_kdf_hkdf_sha512_keybytes() == crypto_kdf_hkdf_sha512_KEYBYTES);
        TEST_ASSERT(crypto_kdf_hkdf_sha512_statebytes() >= sizeof (crypto_kdf_hkdf_sha512_state));

        TEST_ASSERT(crypto_kdf_hkdf_sha256_bytes_min() == crypto_kdf_hkdf_sha256_BYTES_MIN);
        TEST_ASSERT(crypto_kdf_hkdf_sha256_bytes_max() == crypto_kdf_hkdf_sha256_BYTES_MAX);
        TEST_ASSERT(crypto_kdf_hkdf_sha256_keybytes() == crypto_kdf_hkdf_sha256_KEYBYTES);
        TEST_ASSERT(crypto_kdf_hkdf_sha256_statebytes() >= sizeof (crypto_kdf_hkdf_sha256_state));

        TEST_ASSERT(crypto_kdf_hkdf_sha256_KEYBYTES < crypto_kdf_hkdf_sha512_KEYBYTES);

        DebugPrint("tv_kdf_hkdf: ok\n");
    }

    int
    test(void)
    {
        tv_kdf_hkdf();

        return 0;
    }

    void _libsodium_ut_kdf_hkdf()
    {
        test();
    }
} // namespace
