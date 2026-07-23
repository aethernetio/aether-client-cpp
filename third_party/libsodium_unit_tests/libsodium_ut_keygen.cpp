#include "libsodium_ut_keygen.h"

#include "unity.h"
//#include "aether/tele.h"

namespace keygen
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
    tv_keygen(void)
    {
        static const KeygenTV tvs[] = {
            { crypto_auth_keygen, crypto_auth_KEYBYTES },
            { crypto_auth_hmacsha256_keygen, crypto_auth_hmacsha256_KEYBYTES },
            { crypto_aead_aes256gcm_keygen, crypto_aead_aes256gcm_KEYBYTES },
            { crypto_auth_hmacsha512_keygen, crypto_auth_hmacsha512_KEYBYTES },
            { crypto_auth_hmacsha512256_keygen, crypto_auth_hmacsha512256_KEYBYTES },
            { crypto_generichash_keygen, crypto_generichash_KEYBYTES },
            { crypto_generichash_blake2b_keygen, crypto_generichash_blake2b_KEYBYTES },
            { crypto_kdf_keygen, crypto_kdf_KEYBYTES },
            { crypto_onetimeauth_keygen, crypto_onetimeauth_KEYBYTES },
            { crypto_onetimeauth_poly1305_keygen, crypto_onetimeauth_poly1305_KEYBYTES },
            { crypto_aead_chacha20poly1305_ietf_keygen, crypto_aead_chacha20poly1305_ietf_KEYBYTES },
            { crypto_aead_chacha20poly1305_keygen, crypto_aead_chacha20poly1305_KEYBYTES },
            { crypto_aead_chacha20poly1305_ietf_keygen, crypto_aead_chacha20poly1305_ietf_KEYBYTES },
            { crypto_aead_xchacha20poly1305_ietf_keygen, crypto_aead_xchacha20poly1305_ietf_KEYBYTES },
            { crypto_secretbox_xsalsa20poly1305_keygen, crypto_secretbox_xsalsa20poly1305_KEYBYTES },
            { crypto_secretbox_keygen, crypto_secretbox_KEYBYTES },
            { crypto_secretstream_xchacha20poly1305_keygen, crypto_secretstream_xchacha20poly1305_KEYBYTES },
            { crypto_shorthash_keygen, crypto_shorthash_KEYBYTES },
            { crypto_stream_keygen, crypto_stream_KEYBYTES },
            { crypto_stream_chacha20_keygen, crypto_stream_chacha20_KEYBYTES },
            { crypto_stream_chacha20_ietf_keygen, crypto_stream_chacha20_ietf_KEYBYTES },
            { crypto_stream_salsa20_keygen, crypto_stream_salsa20_KEYBYTES },
            { crypto_stream_xsalsa20_keygen, crypto_stream_xsalsa20_KEYBYTES }
        };
        const KeygenTV *tv;
        unsigned char  *key;
        size_t          i;
        int             j;

        for (i = 0; i < (sizeof tvs) / (sizeof tvs[0]); i++) {
            tv = &tvs[i];
            key = (unsigned char *) sodium_malloc(tv->key_len);
            key[tv->key_len - 1U] = 0;
            for (j = 0; j < 10000; j++) {
                tv->fn(key);
                if (key[tv->key_len - 1U] != 0) {
                    break;
                }
            }
            sodium_free(key);
            if (j >= 10000) {
                DebugPrint("Buffer underflow with test vector %u\n", (unsigned int) i);
            }
        }
        DebugPrint("tv_keygen: ok\n");
    }

    int
    test(void)
    {
        tv_keygen();

        return 0;
    }

    void _libsodium_ut_keygen()
    {
        test();
    }
} // namespace
