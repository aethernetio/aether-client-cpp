///
///\file main.cpp
///\brief AetherNet library example
/// Unit tests for the hydrogen library
///
/// This example code is in the Public Domain (or CC0 licensed, at your option.)
///
/// Unless required by applicable law or agreed to in writing, this
/// software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
/// CONDITIONS OF ANY KIND, either express or implied.
///
///\author Dmitriy Kiryanov dmitriyk@aethernet.io
///\version 1.0.0
///\date  12.04.2024
///

#include "libsodium_unit_tests.h"

//#include "aether/tele.h"
#include "libsodium_ut_aead_aegis128l.h"
#include "libsodium_ut_aead_aegis256.h"
#include "libsodium_ut_aead_aes256gcm.h"
#include "libsodium_ut_aead_aes256gcm2.h"
#include "libsodium_ut_aead_chacha20poly1305.h"
#include "libsodium_ut_aead_chacha20poly13052.h"
#include "libsodium_ut_aead_xchacha20poly1305.h"
#include "libsodium_ut_auth.h"
#include "libsodium_ut_auth2.h"
#include "libsodium_ut_auth3.h"
#include "libsodium_ut_auth5.h"
#include "libsodium_ut_auth6.h"
#include "libsodium_ut_auth7.h"
#include "libsodium_ut_box.h"
#include "libsodium_ut_box_easy.h"
#include "libsodium_ut_box_easy2.h"
#include "libsodium_ut_box_seal.h"
#include "libsodium_ut_box_seed.h"
#include "libsodium_ut_box2.h"
#include "libsodium_ut_box7.h"
#include "libsodium_ut_box8.h"
#include "libsodium_ut_chacha20.h"
#include "libsodium_ut_codecs.h"
#include "libsodium_ut_core_ed25519.h"
#include "libsodium_ut_core_ed25519_h2c.h"
#include "libsodium_ut_core_ristretto255.h"
#include "libsodium_ut_core1.h"
#include "libsodium_ut_core2.h"
#include "libsodium_ut_core3.h"
#include "libsodium_ut_core4.h"
#include "libsodium_ut_core5.h"
#include "libsodium_ut_core6.h"
#include "libsodium_ut_ed25519_convert.h"
#include "libsodium_ut_generichash.h"
#include "libsodium_ut_generichash2.h"
#include "libsodium_ut_generichash3.h"
#include "libsodium_ut_hash.h"
#include "libsodium_ut_hash3.h"
#include "libsodium_ut_kdf.h"
#include "libsodium_ut_kdf_hkdf.h"
#include "libsodium_ut_keygen.h"
#include "libsodium_ut_kx.h"
#include "libsodium_ut_metamorphic.h"
#include "libsodium_ut_misuse.h"
#include "libsodium_ut_onetimeauth.h"
#include "libsodium_ut_onetimeauth2.h"
#include "libsodium_ut_onetimeauth7.h"
#include "libsodium_ut_pwhash_argon2i.h"
#include "libsodium_ut_pwhash_argon2id.h"
#include "libsodium_ut_pwhash_scrypt.h"
#include "libsodium_ut_pwhash_scrypt_ll.h"
#include "libsodium_ut_randombytes.h"
#include "libsodium_ut_scalarmult.h"
#include "libsodium_ut_scalarmult_ed25519.h"
#include "libsodium_ut_scalarmult_ristretto255.h"
#include "libsodium_ut_scalarmult2.h"
#include "libsodium_ut_scalarmult5.h"
#include "libsodium_ut_scalarmult6.h"
#include "libsodium_ut_scalarmult7.h"
#include "libsodium_ut_scalarmult8.h"
#include "libsodium_ut_secretbox.h"
#include "libsodium_ut_secretbox_easy.h"
#include "libsodium_ut_secretbox_easy2.h"
#include "libsodium_ut_secretbox2.h"
#include "libsodium_ut_secretbox7.h"
#include "libsodium_ut_secretbox8.h"
#include "libsodium_ut_secretstream_xchacha20poly1305.h"
#include "libsodium_ut_shorthash.h"
#include "libsodium_ut_sign.h"
#include "libsodium_ut_sign2.h"
#include "libsodium_ut_siphashx24.h"
#include "libsodium_ut_sodium_utils.h"
//#include "libsodium_ut_sodium_utils2.h"
//#include "libsodium_ut_sodium_utils3.h"
#include "libsodium_ut_sodium_version.h"
#include "libsodium_ut_stream.h"
#include "libsodium_ut_stream2.h"
#include "libsodium_ut_stream3.h"
#include "libsodium_ut_stream4.h"
#include "libsodium_ut_verify1.h"
#include "libsodium_ut_xchacha20.h"


#include <unity.h>

#define DEBUG_OUT 1
#define LOG(TAG, ...) {\
 fprintf(stderr, "file location%s\nfunction name:%s\nline:%d\n", __FILE__, __FUNCTION__, __LINE__);\
 fprintf(stderr, "date:%s\ntime:%s\n",__DATE__, __TIME__);\
 fprintf(stderr, "%s", TAG);\
 fprintf(stderr, "\r\n");\
 fprintf(stderr, __VA_ARGS__);\
 fprintf(stderr, "\n");\
}

static const char *TAG = "HYDROGEN UNITY";

#if DEBUG_OUT==1
    #define DebugPrint(format, ...) LOG(TAG, format, ##__VA_ARGS__)
#elif DEBUG_OUT==2
    #define DebugPrint(format, ...) AE_TELED_DEBUG(TAG, format, ##__VA_ARGS__)
#elif DEBUG_OUT==3
    #define DebugPrint(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
#endif

///
///\brief libsodium_unit_tests_runner Unit test runner for libsodium.
///\param[in] void
///\return void
///
int libsodium_unit_tests_runner()
{
	UNITY_BEGIN();

    DebugPrint("Starting libsodium unit tests.");

	RUN_TEST(aead_aegis128l::_libsodium_ut_aead_aegis128l);
    RUN_TEST(aead_aegis256::_libsodium_ut_aead_aegis256);
    RUN_TEST(aead_aes256gcm::_libsodium_ut_aead_aes256gcm);
    RUN_TEST(aead_aes256gcm2::_libsodium_ut_aead_aes256gcm2);
    RUN_TEST(aead_chacha20poly1305::_libsodium_ut_aead_chacha20poly1305);
    RUN_TEST(aead_chacha20poly13052::_libsodium_ut_aead_chacha20poly13052);
    RUN_TEST(aead_xchacha20poly1305::_libsodium_ut_aead_xchacha20poly1305);
    RUN_TEST(auth::_libsodium_ut_auth);
    RUN_TEST(auth2::_libsodium_ut_auth2);
    RUN_TEST(auth3::_libsodium_ut_auth3);
    RUN_TEST(auth5::_libsodium_ut_auth5);
    RUN_TEST(auth6::_libsodium_ut_auth6);
    RUN_TEST(auth7::_libsodium_ut_auth7);
    RUN_TEST(box::_libsodium_ut_box);
    RUN_TEST(box_easy::_libsodium_ut_box_easy);
    RUN_TEST(box_easy2::_libsodium_ut_box_easy2);
    RUN_TEST(box_seal::_libsodium_ut_box_seal);
    RUN_TEST(box_seed::_libsodium_ut_box_seed);
    RUN_TEST(box2::_libsodium_ut_box2);
    RUN_TEST(box7::_libsodium_ut_box7);
    RUN_TEST(box8::_libsodium_ut_box8);
    RUN_TEST(chacha20::_libsodium_ut_chacha20);
    RUN_TEST(codecs::_libsodium_ut_codecs);
    RUN_TEST(core_ed25519::_libsodium_ut_core_ed25519);
    RUN_TEST(core_ed25519_h2c::_libsodium_ut_core_ed25519_h2c);
    RUN_TEST(core_ristretto255::_libsodium_ut_core_ristretto255);
    RUN_TEST(core1::_libsodium_ut_core1);
    RUN_TEST(core2::_libsodium_ut_core2);
    RUN_TEST(core3::_libsodium_ut_core3);
    RUN_TEST(core4::_libsodium_ut_core4);
    RUN_TEST(core5::_libsodium_ut_core5);
    RUN_TEST(core6::_libsodium_ut_core6);
    RUN_TEST(ed25519_convert::_libsodium_ut_ed25519_convert);
    RUN_TEST(generichash::_libsodium_ut_generichash);
    RUN_TEST(generichash2::_libsodium_ut_generichash2);
    RUN_TEST(generichash3::_libsodium_ut_generichash3);
    RUN_TEST(hash::_libsodium_ut_hash);
    RUN_TEST(hash3::_libsodium_ut_hash3);
    RUN_TEST(kdf::_libsodium_ut_kdf);
    RUN_TEST(kdf_hkdf::_libsodium_ut_kdf_hkdf);
    RUN_TEST(keygen::_libsodium_ut_keygen);
    RUN_TEST(kx::_libsodium_ut_kx);
    RUN_TEST(metamorphic::_libsodium_ut_metamorphic);
    RUN_TEST(misuse::_libsodium_ut_misuse);
    RUN_TEST(onetimeauth::_libsodium_ut_onetimeauth);
    RUN_TEST(onetimeauth2::_libsodium_ut_onetimeauth2);
    RUN_TEST(onetimeauth7::_libsodium_ut_onetimeauth7);
    RUN_TEST(pwhash_argon2i::_libsodium_ut_pwhash_argon2i);
    RUN_TEST(pwhash_argon2id::_libsodium_ut_pwhash_argon2id);
    RUN_TEST(pwhash_scrypt::_libsodium_ut_pwhash_scrypt);
    RUN_TEST(pwhash_scrypt_ll::_libsodium_ut_pwhash_scrypt_ll);
    RUN_TEST(randombytes_sodium::_libsodium_ut_randombytes);
    RUN_TEST(scalarmult::_libsodium_ut_scalarmult);
    RUN_TEST(scalarmult_ed25519::_libsodium_ut_scalarmult_ed25519);
    RUN_TEST(scalarmult_ristretto255::_libsodium_ut_scalarmult_ristretto255);
    RUN_TEST(scalarmult2::_libsodium_ut_scalarmult2);
    RUN_TEST(scalarmult5::_libsodium_ut_scalarmult5);
    RUN_TEST(scalarmult6::_libsodium_ut_scalarmult6);
    RUN_TEST(scalarmult7::_libsodium_ut_scalarmult7);
    RUN_TEST(scalarmult8::_libsodium_ut_scalarmult8);
    RUN_TEST(secretbox::_libsodium_ut_secretbox);
    RUN_TEST(secretbox_easy::_libsodium_ut_secretbox_easy);
    RUN_TEST(secretbox_easy2::_libsodium_ut_secretbox_easy2);
    RUN_TEST(secretbox2::_libsodium_ut_secretbox2);
    RUN_TEST(secretbox7::_libsodium_ut_secretbox7);
    RUN_TEST(secretbox8::_libsodium_ut_secretbox8);
    RUN_TEST(secretstream_xchacha20poly1305::_libsodium_ut_secretstream_xchacha20poly1305);
    RUN_TEST(shorthash::_libsodium_ut_shorthash);
    RUN_TEST(sign::_libsodium_ut_sign);
    RUN_TEST(sign2::_libsodium_ut_sign2);
    RUN_TEST(siphashx24::_libsodium_ut_siphashx24);
    RUN_TEST(sodium_utils::_libsodium_ut_sodium_utils);
    //RUN_TEST(sodium_utils2::_libsodium_ut_sodium_utils2);
    //RUN_TEST(sodium_utils3::_libsodium_ut_sodium_utils3);
    RUN_TEST(sodium_version::_libsodium_ut_sodium_version);
    RUN_TEST(stream::_libsodium_ut_stream);
    RUN_TEST(stream2::_libsodium_ut_stream2);
    RUN_TEST(stream3::_libsodium_ut_stream3);
    RUN_TEST(stream4::_libsodium_ut_stream4);
    RUN_TEST(verify1::_libsodium_ut_verify1);
    RUN_TEST(xchacha20::_libsodium_ut_xchacha20);

	return UNITY_END(); // stop unit testing
}
