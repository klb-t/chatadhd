package com.chatadhd.android

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.Key
import java.security.KeyStore
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

/**
 * The AES-256-GCM key [SecretsVaultCore] seals secrets.json with. It lives in the Android
 * Keystore and never leaves it: the app only ever asks the Keystore to encrypt or decrypt.
 *
 * The key is not bound to a lock screen or biometrics (the app must be able to start unattended,
 * e.g. for the background semantic worker); binding it is a possible future setting. It does not
 * survive an uninstall or a device restore, which is intended: a sealed secrets file that is
 * copied elsewhere (cloud backup, another phone, a leftover in shared storage) cannot be opened
 * without this key, and the app then asks for the API keys again instead of failing.
 */
internal object KeystoreKeys : SecretsVaultCore.KeyProvider {
    private const val PROVIDER = "AndroidKeyStore"
    private const val ALIAS = "chatadhd.secrets.v1"

    @Synchronized
    override fun key(): Key {
        val ks = KeyStore.getInstance(PROVIDER).apply { load(null) }
        (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, PROVIDER)
        gen.init(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setRandomizedEncryptionRequired(true)
                .build()
        )
        return gen.generateKey()
    }
}
