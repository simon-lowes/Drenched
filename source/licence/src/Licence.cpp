#include "drenched/Licence.h"

#include <monocypher-ed25519.h>

#include <algorithm>
#include <cctype>

namespace drenched::licence
{

namespace
{
    // Crockford's base 32: no I, L, O or U.
    constexpr std::string_view alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    constexpr std::string_view keyCheckPrefix = "Drenched licence number ";
    constexpr std::string_view machinePrefix = "Drenched machine ";
    constexpr std::string_view codePrefix = "DRN1.";
    constexpr std::string_view payloadHeader = "Drenched activation 1\n";
    constexpr std::size_t maxPayload = 1024;
    constexpr std::size_t maxName = 200;

    int symbolValue (char c) noexcept
    {
        auto upper = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));

        if (upper == 'O')
            upper = '0';
        else if (upper == 'I' || upper == 'L')
            upper = '1';

        const auto at = alphabet.find (upper);
        return at == std::string_view::npos ? -1 : static_cast<int> (at);
    }

    std::array<std::uint8_t, 64> sha512 (std::string_view a, std::string_view b)
    {
        crypto_sha512_ctx ctx;
        crypto_sha512_init (&ctx);
        crypto_sha512_update (&ctx, reinterpret_cast<const std::uint8_t*> (a.data()), a.size());
        crypto_sha512_update (&ctx, reinterpret_cast<const std::uint8_t*> (b.data()), b.size());
        std::array<std::uint8_t, 64> hash {};
        crypto_sha512_final (&ctx, hash.data());
        return hash;
    }

    /** Symbols from the leading bits of some bytes, most significant first. */
    std::string symbolsFrom (std::span<const std::uint8_t> bytes, std::size_t count)
    {
        std::string out;
        out.reserve (count);

        for (std::size_t i = 0; i < count; ++i)
        {
            unsigned value = 0;

            for (std::size_t bit = i * 5; bit < i * 5 + 5; ++bit)
                value = (value << 1) | ((bytes[bit / 8] >> (7 - bit % 8)) & 1u);

            out += alphabet[value];
        }

        return out;
    }

    std::string checkSymbols (std::string_view body)
    {
        const auto hash = sha512 (keyCheckPrefix, body);
        return { alphabet[hash[0] >> 3], alphabet[hash[1] >> 3] };
    }

    std::string grouped (std::string_view raw)
    {
        std::string out;

        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            if (i > 0 && i % 5 == 0)
                out += '-';

            out += raw[i];
        }

        return out;
    }

    /** The symbols someone typed, in canonical form, if there are exactly length of them. */
    std::optional<std::string> canonicalSymbols (std::string_view typed, std::size_t length)
    {
        std::string raw;

        for (const auto c : typed)
        {
            if (c == '-' || std::isspace (static_cast<unsigned char> (c)))
                continue;

            const auto value = symbolValue (c);

            if (value < 0 || raw.size() == length)
                return std::nullopt;

            raw += alphabet[static_cast<std::size_t> (value)];
        }

        if (raw.size() != length)
            return std::nullopt;

        return raw;
    }

    std::optional<std::string> base64UrlDecode (std::string_view text)
    {
        std::string out;
        unsigned buffer = 0;
        int bits = 0;

        for (const auto c : text)
        {
            int value;

            if (c >= 'A' && c <= 'Z')      value = c - 'A';
            else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
            else if (c >= '0' && c <= '9') value = c - '0' + 52;
            else if (c == '-')             value = 62;
            else if (c == '_')             value = 63;
            else                           return std::nullopt;

            buffer = (buffer << 6) | static_cast<unsigned> (value);
            bits += 6;

            if (bits >= 8)
            {
                bits -= 8;
                out += static_cast<char> ((buffer >> bits) & 0xffu);
            }
        }

        // Leftover bits must be padding zeros, so each code has one spelling.
        if (bits >= 6 || (buffer & ((1u << bits) - 1u)) != 0)
            return std::nullopt;

        return out;
    }

    bool isDate (std::string_view s) noexcept
    {
        if (s.size() != 10 || s[4] != '-' || s[7] != '-')
            return false;

        for (std::size_t i = 0; i < s.size(); ++i)
            if (i != 4 && i != 7 && ! std::isdigit (static_cast<unsigned char> (s[i])))
                return false;

        return true;
    }

    /** Reads "label: value\n" from the front of text. */
    std::optional<std::string> field (std::string_view& text, std::string_view label)
    {
        if (text.substr (0, label.size()) != label || text.substr (label.size(), 2) != ": ")
            return std::nullopt;

        text.remove_prefix (label.size() + 2);
        const auto end = text.find ('\n');

        if (end == std::string_view::npos)
            return std::nullopt;

        std::string value (text.substr (0, end));
        text.remove_prefix (end + 1);
        return value;
    }
}

//==============================================================================
std::string makeKey (std::span<const std::uint8_t, 15> randomBytes)
{
    const auto body = symbolsFrom (randomBytes, keyLength - 2);
    return grouped (body + checkSymbols (body));
}

std::optional<std::string> parseKey (std::string_view typed)
{
    const auto raw = canonicalSymbols (typed, keyLength);

    if (! raw.has_value())
        return std::nullopt;

    const std::string_view body (raw->data(), keyLength - 2);

    if (raw->substr (keyLength - 2) != checkSymbols (body))
        return std::nullopt;

    return grouped (*raw);
}

std::string machineCode (std::string_view deviceIdentifier)
{
    const auto hash = sha512 (machinePrefix, deviceIdentifier);
    return grouped (symbolsFrom (hash, machineLength));
}

std::optional<std::string> parseMachineCode (std::string_view typed)
{
    const auto raw = canonicalSymbols (typed, machineLength);
    return raw.has_value() ? std::optional (grouped (*raw)) : std::nullopt;
}

std::string activationPayload (const Activation& a)
{
    return std::string (payloadHeader)
         + "licence: " + a.key + "\n"
         + "machine: " + a.machine + "\n"
         + "name: " + a.name + "\n"
         + "issued: " + a.issued + "\n";
}

std::optional<Activation> verifyActivationCode (std::string_view code, const PublicKey& key)
{
    // Copying from an email or a web page can wrap or indent the code.
    std::string compact;

    for (const auto c : code)
        if (! std::isspace (static_cast<unsigned char> (c)))
            compact += c;

    std::string_view text (compact);

    if (text.substr (0, codePrefix.size()) != codePrefix)
        return std::nullopt;

    text.remove_prefix (codePrefix.size());
    const auto dot = text.find ('.');

    if (dot == std::string_view::npos || text.size() > 2 * maxPayload)
        return std::nullopt;

    const auto payload = base64UrlDecode (text.substr (0, dot));
    const auto signature = base64UrlDecode (text.substr (dot + 1));

    if (! payload.has_value() || ! signature.has_value() || signature->size() != 64 || payload->size() > maxPayload)
        return std::nullopt;

    if (crypto_ed25519_check (reinterpret_cast<const std::uint8_t*> (signature->data()), key.data(),
                              reinterpret_cast<const std::uint8_t*> (payload->data()), payload->size()) != 0)
        return std::nullopt;

    // Signed, so it came from the server; still read it strictly.
    std::string_view rest (*payload);

    if (rest.substr (0, payloadHeader.size()) != payloadHeader)
        return std::nullopt;

    rest.remove_prefix (payloadHeader.size());

    Activation a;
    const auto licenceField = field (rest, "licence");
    const auto machineField = field (rest, "machine");
    const auto nameField = field (rest, "name");
    const auto issuedField = field (rest, "issued");

    if (! licenceField || ! machineField || ! nameField || ! issuedField || ! rest.empty())
        return std::nullopt;

    a.key = *licenceField;
    a.machine = *machineField;
    a.name = *nameField;
    a.issued = *issuedField;

    const auto controlCharacter = [] (char c) { return static_cast<unsigned char> (c) < 0x20 || c == 0x7f; };

    if (parseKey (a.key) != a.key || parseMachineCode (a.machine) != a.machine || ! isDate (a.issued)
        || a.name.size() > maxName || std::any_of (a.name.begin(), a.name.end(), controlCharacter))
        return std::nullopt;

    return a;
}

int trialDaysLeft (std::int64_t firstDay, std::int64_t today) noexcept
{
    const auto elapsed = std::max<std::int64_t> (0, today - firstDay);
    return static_cast<int> (std::clamp<std::int64_t> (trialDays - elapsed, 0, trialDays));
}

const PublicKey& testKey() noexcept
{
    // From the seed 01 02 03 ... 20 (hex); see tests/LicenceTests.cpp and server/test.
    static constexpr PublicKey key { 0x79, 0xb5, 0x56, 0x2e, 0x8f, 0xe6, 0x54, 0xf9, 0x40, 0x78, 0xb1, 0x12, 0xe8, 0xa9, 0x8b, 0xa7,
                                     0x90, 0x1f, 0x85, 0x3a, 0xe6, 0x95, 0xbe, 0xd7, 0xe0, 0xe3, 0x91, 0x0b, 0xad, 0x04, 0x96, 0x64 };
    return key;
}

const PublicKey& serverKey() noexcept
{
    // server/scripts/make-keys.mjs writes the production key here when the
    // activation server is deployed.
    // BEGIN SERVER KEY
    return testKey();
    // END SERVER KEY
}

bool usingTestKey() noexcept
{
    return serverKey() == testKey();
}

} // namespace drenched::licence
