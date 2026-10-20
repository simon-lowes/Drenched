#include <drenched/Licence.h>

#include <monocypher-ed25519.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <random>
#include <string>

using namespace drenched;

namespace
{
    // The same vectors as server/test/vectors.json, so the plugin and the
    // activation server cannot drift apart.
    constexpr auto serverCode =
        "DRN1.RHJlbmNoZWQgYWN0aXZhdGlvbiAxCmxpY2VuY2U6IDBDQTJBLURKN0IxLU1RTjJXLVdOUFpDLVpSN0NTCm1hY2hpbmU6IDJDWTlBLUdLUERaLTZWSDlOLVozU0JQCm5hbWU6IEpvc8OpIE3DvGxsZXIKaXNzdWVkOiAyMDI2LTEwLTIwCg"
        ".MoBFqTmIt_1Hn6JwYM6Yd7Q1gngWgLdXpRNtb3BfIO26XB_uFt7Jf2iT4o66SxCXbu2glWs6CpkM7uE17cGkDA";

    const licence::Activation serverActivation { "0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS", "2CY9A-GKPDZ-6VH9N-Z3SBP",
                                                 "Jos\xc3\xa9 M\xc3\xbcller", "2026-10-20" };

    std::array<std::uint8_t, 15> bytes (std::uint8_t first, std::uint8_t step)
    {
        std::array<std::uint8_t, 15> b {};

        for (std::size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<std::uint8_t> (first + step * i);

        return b;
    }

    /** Signs an activation with the test key, as the server does. */
    std::string sign (const std::string& payload)
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

TEST_CASE ("Licence numbers match the activation server's", "[licence]")
{
    CHECK (licence::makeKey (bytes (3, 17)) == "0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS");
    CHECK (licence::makeKey (bytes (255, 0)) == "ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ-ZZZV2");
    CHECK (licence::makeKey (bytes (0, 0)) == "00000-00000-00000-00000-0004C");

    CHECK (licence::machineCode ("") == "RQYDP-JPBM7-F21TS-70VN5");
    CHECK (licence::machineCode ("test-device") == "2CY9A-GKPDZ-6VH9N-Z3SBP");
    CHECK (licence::machineCode ("C02XK0ABJGH5:\xc3\xbcn\xc3\xaf" "code") == "Y93Z1-YGCYZ-5N6QR-V48RW");
}

TEST_CASE ("Licence numbers forgive how they are typed, not what is typed", "[licence]")
{
    const std::string key = "0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS";

    CHECK (licence::parseKey (key) == key);
    CHECK (licence::parseKey ("  0ca2a dj7b1 mqn2w wnpzc zr7cs\n") == key);
    CHECK (licence::parseKey ("OCA2ADJ7BIMQN2WWNPZCZR7CS") == key); // O for 0, I for 1
    CHECK (licence::parseKey ("0CA2A-DJ7Bl-MQN2W-WNPZC-ZR7CS") == key); // l for 1

    CHECK_FALSE (licence::parseKey ("").has_value());
    CHECK_FALSE (licence::parseKey ("0CA2A-DJ7B1-MQN2W-WNPZC-ZR7C").has_value());   // one short
    CHECK_FALSE (licence::parseKey ("0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CSS").has_value()); // one long
    CHECK_FALSE (licence::parseKey ("0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CU").has_value());  // U is not used

    // Every single mistyped character and every swapped neighbour pair is caught,
    // except for the 1 in 1024 that happen to match the check characters.
    constexpr std::string_view alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::mt19937 rng (42);
    int typos = 0, caught = 0;

    for (int n = 0; n < 200; ++n)
    {
        std::array<std::uint8_t, 15> random {};

        for (auto& b : random)
            b = static_cast<std::uint8_t> (rng() & 0xffu);

        const auto good = licence::makeKey (random);
        REQUIRE (licence::parseKey (good) == good);

        for (std::size_t i = 0; i < good.size(); ++i)
        {
            if (good[i] == '-')
                continue;

            auto bad = good;
            bad[i] = alphabet[(alphabet.find (good[i]) + 1 + rng() % 31) % 32];
            ++typos;
            caught += licence::parseKey (bad).has_value() ? 0 : 1;

            if (i + 1 < good.size() && good[i + 1] != '-' && good[i] != good[i + 1])
            {
                auto swapped = good;
                std::swap (swapped[i], swapped[i + 1]);
                ++typos;
                caught += licence::parseKey (swapped).has_value() ? 0 : 1;
            }
        }
    }

    CHECK (caught >= typos - typos / 200);
}

TEST_CASE ("Machine codes are tidied but not checked", "[licence]")
{
    CHECK (licence::parseMachineCode ("2cy9a gkpdz 6vh9n z3sbp") == "2CY9A-GKPDZ-6VH9N-Z3SBP");
    CHECK_FALSE (licence::parseMachineCode ("2CY9A-GKPDZ-6VH9N").has_value());
    CHECK_FALSE (licence::parseMachineCode ("0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS").has_value());
}

TEST_CASE ("A code signed by the activation server is accepted", "[licence]")
{
    CHECK (licence::activationPayload (serverActivation)
           == "Drenched activation 1\nlicence: 0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS\nmachine: 2CY9A-GKPDZ-6VH9N-Z3SBP\n"
              "name: Jos\xc3\xa9 M\xc3\xbcller\nissued: 2026-10-20\n");

    CHECK (licence::verifyActivationCode (serverCode, licence::testKey()) == serverActivation);
    CHECK (sign (licence::activationPayload (serverActivation)) == serverCode);

    // Wrapped and indented, as it might arrive by email.
    std::string wrapped = "  ";
    const std::string code (serverCode);

    for (std::size_t i = 0; i < code.size(); i += 40)
        wrapped += code.substr (i, 40) + "\r\n    ";

    CHECK (licence::verifyActivationCode (wrapped, licence::testKey()) == serverActivation);
}

TEST_CASE ("Altered or foreign codes are refused", "[licence]")
{
    const std::string code (serverCode);

    // Any changed character breaks the signature or the encoding.
    for (std::size_t i = 5; i < code.size(); i += 7)
    {
        auto bad = code;
        bad[i] = bad[i] == 'A' ? 'B' : 'A';
        CHECK_FALSE (licence::verifyActivationCode (bad, licence::testKey()).has_value());
    }

    // Another key's signature.
    auto otherKey = licence::testKey();
    otherKey[0] ^= 1;
    CHECK_FALSE (licence::verifyActivationCode (code, otherKey).has_value());

    // A new name needs a new signature.
    auto renamed = serverActivation;
    renamed.name = "Someone Else";
    CHECK (licence::verifyActivationCode (sign (licence::activationPayload (renamed)), licence::testKey())->name == "Someone Else");

    CHECK_FALSE (licence::verifyActivationCode ("", licence::testKey()).has_value());
    CHECK_FALSE (licence::verifyActivationCode ("DRN1.", licence::testKey()).has_value());
    CHECK_FALSE (licence::verifyActivationCode (code.substr (5), licence::testKey()).has_value());
    CHECK_FALSE (licence::verifyActivationCode ("DRN2" + code.substr (4), licence::testKey()).has_value());
}

TEST_CASE ("Signed but malformed activations are refused", "[licence]")
{
    const auto refused = [] (const std::string& payload) { return ! licence::verifyActivationCode (sign (payload), licence::testKey()).has_value(); };
    const auto good = licence::activationPayload (serverActivation);

    CHECK_FALSE (refused (good));
    CHECK (refused (good + "extra: 1\n"));
    CHECK (refused (good.substr (0, good.size() - 1)));
    CHECK (refused ("Drenched activation 2" + good.substr (21)));

    auto a = serverActivation;
    a.key = "0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CT"; // check characters wrong
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation;
    a.key = "0ca2a-dj7b1-mqn2w-wnpzc-zr7cs"; // not canonical
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation;
    a.machine = "2CY9A-GKPDZ-6VH9N";
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation;
    a.issued = "20 October 2026";
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation;
    a.name = "Tab\there";
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation;
    a.name = std::string (201, 'x');
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation;
    a.name.clear();
    CHECK_FALSE (refused (licence::activationPayload (a)));
}

TEST_CASE ("The trial counts whole days and cannot be wound back", "[licence]")
{
    CHECK (licence::trialDaysLeft (20000, 20000) == 14);
    CHECK (licence::trialDaysLeft (20000, 20001) == 13);
    CHECK (licence::trialDaysLeft (20000, 20013) == 1);
    CHECK (licence::trialDaysLeft (20000, 20014) == 0);
    CHECK (licence::trialDaysLeft (20000, 30000) == 0);
    CHECK (licence::trialDaysLeft (20000, 19000) == 14); // clock set before the first day
}

TEST_CASE ("This build carries the test key until the server is deployed", "[licence]")
{
    std::array<std::uint8_t, 32> seed {};

    for (std::size_t i = 0; i < seed.size(); ++i)
        seed[i] = static_cast<std::uint8_t> (i + 1);

    std::array<std::uint8_t, 64> secret {};
    licence::PublicKey publicKey {};
    crypto_ed25519_key_pair (secret.data(), publicKey.data(), seed.data());

    CHECK (publicKey == licence::testKey());
    CHECK (licence::usingTestKey() == (licence::serverKey() == licence::testKey()));
}
