#include <drenched/Licence.h>

#include "LicenceSigning.h"

#include <monocypher-ed25519.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace drenched;

namespace
{
    /** Just enough JSON to read server/test/vectors.json: objects, arrays and strings.
        Numbers, true, false and null are kept as their text. */
    struct Json
    {
        std::string text;                                  // a string's value
        std::vector<Json> items;                           // an array's
        std::vector<std::string> names;                    // an object's member names,
        std::vector<Json> values;                          // and their values (a pair<string, Json> needs Json complete)

        const Json& operator[] (std::string_view name) const
        {
            for (std::size_t i = 0; i < names.size(); ++i)
                if (names[i] == name)
                    return values[i];

            throw std::runtime_error ("vectors.json has no \"" + std::string (name) + "\"");
        }

        static Json parse (const std::string& source)
        {
            std::size_t at = 0;
            auto value = parseValue (source, at);
            skipSpace (source, at);

            if (at != source.size())
                fail (at);

            return value;
        }

    private:
        [[noreturn]] static void fail (std::size_t at)
        {
            throw std::runtime_error ("vectors.json can't be read at character " + std::to_string (at));
        }

        static void skipSpace (const std::string& s, std::size_t& at)
        {
            while (at < s.size() && std::string_view (" \n\r\t").find (s[at]) != std::string_view::npos)
                ++at;
        }

        static void expect (const std::string& s, std::size_t& at, char c)
        {
            skipSpace (s, at);

            if (at >= s.size() || s[at] != c)
                fail (at);

            ++at;
        }

        static std::uint32_t hex4 (const std::string& s, std::size_t& at)
        {
            if (at + 4 > s.size())
                fail (at);

            const auto value = static_cast<std::uint32_t> (std::stoul (s.substr (at, 4), nullptr, 16));
            at += 4;
            return value;
        }

        static void appendUtf8 (std::string& out, std::uint32_t c)
        {
            const auto byte = [&out] (std::uint32_t b) { out += static_cast<char> (static_cast<unsigned char> (b)); };

            if (c < 0x80)
            {
                byte (c);
            }
            else if (c < 0x800)
            {
                byte (0xc0 | (c >> 6));
                byte (0x80 | (c & 0x3f));
            }
            else if (c < 0x10000)
            {
                byte (0xe0 | (c >> 12));
                byte (0x80 | ((c >> 6) & 0x3f));
                byte (0x80 | (c & 0x3f));
            }
            else
            {
                byte (0xf0 | (c >> 18));
                byte (0x80 | ((c >> 12) & 0x3f));
                byte (0x80 | ((c >> 6) & 0x3f));
                byte (0x80 | (c & 0x3f));
            }
        }

        static std::string parseString (const std::string& s, std::size_t& at)
        {
            expect (s, at, '"');
            std::string out;

            while (at < s.size() && s[at] != '"')
            {
                if (s[at] != '\\')
                {
                    out += s[at++];
                    continue;
                }

                if (++at >= s.size())
                    fail (at);

                switch (const auto c = s[at++]; c)
                {
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        auto code = hex4 (s, at);

                        if (code >= 0xd800 && code < 0xdc00 && s.compare (at, 2, "\\u") == 0)
                        {
                            at += 2;
                            code = 0x10000 + ((code - 0xd800) << 10) + (hex4 (s, at) - 0xdc00);
                        }

                        appendUtf8 (out, code);
                        break;
                    }
                    default: out += c; break; // \" \\ and \/
                }
            }

            expect (s, at, '"');
            return out;
        }

        /** The items of an array or the members of an object, up to `close`. */
        template <typename ReadOne>
        static void parseList (const std::string& s, std::size_t& at, char close, ReadOne&& readOne)
        {
            ++at;
            skipSpace (s, at);

            if (at < s.size() && s[at] == close)
            {
                ++at;
                return;
            }

            for (;;)
            {
                readOne();
                skipSpace (s, at);

                if (at < s.size() && s[at] == ',')
                {
                    ++at;
                    continue;
                }

                expect (s, at, close);
                return;
            }
        }

        static Json parseValue (const std::string& s, std::size_t& at)
        {
            skipSpace (s, at);
            Json value;

            if (at >= s.size())
                fail (at);

            if (s[at] == '{')
            {
                parseList (s, at, '}', [&]
                {
                    auto name = parseString (s, at);
                    expect (s, at, ':');
                    auto member = parseValue (s, at);
                    value.names.push_back (std::move (name));
                    value.values.push_back (std::move (member));
                });
            }
            else if (s[at] == '[')
            {
                parseList (s, at, ']', [&] { value.items.push_back (parseValue (s, at)); });
            }
            else if (s[at] == '"')
            {
                value.text = parseString (s, at);
            }
            else
            {
                while (at < s.size() && std::string_view (",}] \n\r\t").find (s[at]) == std::string_view::npos)
                    value.text += s[at++];

                if (value.text.empty())
                    fail (at);
            }

            return value;
        }
    };

    /** server/test/vectors.json, which the activation server's tests read too, so the
        plugin and the server cannot drift apart (DRENCHED_LICENCE_VECTORS, from
        tests/CMakeLists.txt). */
    const Json& vectors()
    {
        static const Json loaded = []
        {
            std::ifstream in (DRENCHED_LICENCE_VECTORS, std::ios::binary);

            if (! in)
                throw std::runtime_error ("can't open " DRENCHED_LICENCE_VECTORS);

            return Json::parse ({ std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() });
        }();

        return loaded;
    }

    template <std::size_t size>
    std::array<std::uint8_t, size> fromHex (const std::string& hex)
    {
        if (hex.size() != 2 * size || hex.find_first_not_of ("0123456789abcdefABCDEF") != std::string::npos)
            throw std::runtime_error ("\"" + hex + "\" is not " + std::to_string (size) + " bytes in hex");

        std::array<std::uint8_t, size> bytes {};

        for (std::size_t i = 0; i < size; ++i)
            bytes[i] = static_cast<std::uint8_t> (std::stoul (hex.substr (2 * i, 2), nullptr, 16));

        return bytes;
    }

    /** The activation the server signed in the vectors, and the code it signed. */
    licence::Activation serverActivation()
    {
        const auto& a = vectors()["activation"];
        return { a["key"].text, a["machine"].text, a["name"].text, a["issued"].text };
    }

    std::string serverCode() { return vectors()["code"].text; }

    /** Signs an activation with the test key, as the server does. */
    std::string sign (const std::string& payload)
    {
        return testing::signWithTestKey (payload);
    }
}

TEST_CASE ("The activation server's test vectors are all read", "[licence]")
{
    // So a vectors.json this reader misreads can't pass by looping over nothing.
    CHECK (vectors()["keys"].items.size() >= 3);
    CHECK (vectors()["machines"].items.size() >= 3);
    CHECK (fromHex<32> (vectors()["seed"].text)[0] == 1);
    CHECK (serverCode().rfind ("DRN1.", 0) == 0);
    CHECK (serverActivation().name == "Jos\xc3\xa9 M\xc3\xbcller"); // UTF-8 kept as it is in the file
}

TEST_CASE ("Licence numbers match the activation server's", "[licence]")
{
    for (const auto& v : vectors()["keys"].items)
    {
        INFO ("random bytes " << v["random"].text);
        CHECK (licence::makeKey (fromHex<15> (v["random"].text)) == v["key"].text);
    }

    for (const auto& v : vectors()["machines"].items)
    {
        INFO ("device \"" << v["device"].text << "\"");
        CHECK (licence::machineCode (v["device"].text) == v["code"].text);
    }
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
    const auto activation = serverActivation();
    const auto code = serverCode();

    CHECK (licence::activationPayload (activation)
           == "Drenched activation 1\nlicence: " + activation.key + "\nmachine: " + activation.machine + "\nname: " + activation.name
                  + "\nissued: " + activation.issued + "\n");

    CHECK (licence::verifyActivationCode (code, licence::testKey()) == activation);
    CHECK (sign (licence::activationPayload (activation)) == code);

    // Wrapped and indented, as it might arrive by email.
    std::string wrapped = "  ";

    for (std::size_t i = 0; i < code.size(); i += 40)
        wrapped += code.substr (i, 40) + "\r\n    ";

    CHECK (licence::verifyActivationCode (wrapped, licence::testKey()) == activation);
}

TEST_CASE ("Altered or foreign codes are refused", "[licence]")
{
    const auto code = serverCode();

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
    auto renamed = serverActivation();
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
    const auto good = licence::activationPayload (serverActivation());

    CHECK_FALSE (refused (good));
    CHECK (refused (good + "extra: 1\n"));
    CHECK (refused (good.substr (0, good.size() - 1)));
    CHECK (refused ("Drenched activation 2" + good.substr (21)));

    auto a = serverActivation();
    a.key = "0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CT"; // check characters wrong
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation();
    a.key = "0ca2a-dj7b1-mqn2w-wnpzc-zr7cs"; // not canonical
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation();
    a.machine = "2CY9A-GKPDZ-6VH9N";
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation();
    a.issued = "20 October 2026";
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation();
    a.name = "Tab\there";
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation();
    a.name = std::string (201, 'x');
    CHECK (refused (licence::activationPayload (a)));

    a = serverActivation();
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
    // The test key is the one the server's tests sign with.
    auto seed = fromHex<32> (vectors()["seed"].text);
    std::array<std::uint8_t, 64> secret {};
    licence::PublicKey publicKey {};
    crypto_ed25519_key_pair (secret.data(), publicKey.data(), seed.data());

    CHECK (publicKey == licence::testKey());
    CHECK (licence::usingTestKey() == (licence::serverKey() == licence::testKey()));
}
