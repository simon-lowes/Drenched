#pragma once

#include <monocypher-ed25519.h>

#include <array>
#include <cstdint>
#include <string>

namespace testing
{
    /** Signs an activation payload with the test key, as the activation server does, and
        returns the activation code. The test key's seed (01 02 03 ... 20 hex) is published
        here and in server/test, so a code it signs is worthless outside development; a
        release build refuses to carry its public half (source/plugin/CMakeLists.txt). */
    inline std::string signWithTestKey (const std::string& payload)
    {
        std::array<std::uint8_t, 32> seed {};

        for (std::size_t i = 0; i < seed.size(); ++i)
            seed[i] = static_cast<std::uint8_t> (i + 1);

        std::array<std::uint8_t, 64> secret {};
        std::array<std::uint8_t, 32> publicKey {};
        crypto_ed25519_key_pair (secret.data(), publicKey.data(), seed.data());

        std::array<std::uint8_t, 64> signature {};
        crypto_ed25519_sign (signature.data(), secret.data(), reinterpret_cast<const std::uint8_t*> (payload.data()), payload.size());

        const auto base64Url = [] (const std::uint8_t* data, std::size_t size)
        {
            static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::string out;
            unsigned buffer = 0;
            int bits = 0;

            for (std::size_t i = 0; i < size; ++i)
            {
                buffer = (buffer << 8) | data[i];
                bits += 8;

                while (bits >= 6)
                {
                    bits -= 6;
                    out += table[(buffer >> bits) & 63u];
                }
            }

            if (bits > 0)
                out += table[(buffer << (6 - bits)) & 63u];

            return out;
        };

        return "DRN1." + base64Url (reinterpret_cast<const std::uint8_t*> (payload.data()), payload.size()) + "."
             + base64Url (signature.data(), signature.size());
    }
}
