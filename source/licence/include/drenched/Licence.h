#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace drenched
{

/** Licence numbers and activation codes. Plain C++20 with no JUCE dependency.

    A licence number is 25 characters in five groups of five, for example
    "7Q2KD-M9XRT-0BW4H-NC8VZ-3JPA6": 23 random characters (115 bits, so numbers
    cannot be guessed) and 2 check characters, so a typing mistake is caught
    before anything goes online. The characters are Crockford's base 32: digits
    and capitals without I, L, O and U, so nothing reads as anything else.

    The activation server answers a licence number and a machine code with an
    activation code: the licence, the machine, the buyer's name and the date,
    signed with the server's Ed25519 key. The plugin checks that signature with
    the public key it carries, so after one online activation it works offline
    for good. The same code can be carried over by hand from another computer
    when the studio machine has no internet. */
namespace licence
{
    inline constexpr std::size_t keyLength = 25;     // characters, without dashes
    inline constexpr std::size_t machineLength = 20;
    inline constexpr int trialDays = 14;
    inline constexpr int computersPerLicence = 3;

    using PublicKey = std::array<std::uint8_t, 32>;

    /** A new licence number from 15 random bytes (only 115 bits are used). */
    std::string makeKey (std::span<const std::uint8_t, 15> randomBytes);

    /** The licence number a person typed, tidied up (any case, spaces or dashes,
        O for zero, I or L for one), in its canonical XXXXX-XXXXX-XXXXX-XXXXX-XXXXX
        form; nothing if it is not a licence number or a character is mistyped. */
    std::optional<std::string> parseKey (std::string_view typed);

    /** This computer's code (XXXXX-XXXXX-XXXXX-XXXXX), from an identifier the
        operating system keeps for the machine. The identifier is hashed, so the
        code says nothing about the machine. */
    std::string machineCode (std::string_view deviceIdentifier);

    /** Tidies up a machine code someone typed; nothing if it is not one. */
    std::optional<std::string> parseMachineCode (std::string_view typed);

    struct Activation
    {
        std::string key;
        std::string machine;
        std::string name;
        std::string issued; // YYYY-MM-DD

        bool operator== (const Activation&) const = default;
    };

    /** The activation inside a code, if the code is signed by the key and well
        formed. Surrounding spaces and line breaks (from copying) are ignored.
        It does not compare the machine; the caller does. */
    std::optional<Activation> verifyActivationCode (std::string_view code, const PublicKey& key);

    /** The text that is signed for an activation. The server builds the same. */
    std::string activationPayload (const Activation&);

    /** Days of the trial left on a given day (days since 1970, UTC), for a trial
        that began on firstDay. A clock set back does not add days. */
    int trialDaysLeft (std::int64_t firstDay, std::int64_t today) noexcept;

    /** The public half of the activation server's signing key. */
    const PublicKey& serverKey() noexcept;

    /** True while the plugin carries the published test key instead of the
        production one, which is set when the activation server is deployed. A
        build carrying the test key must not be released. */
    bool usingTestKey() noexcept;

    /** The test signing key's public half. Its private half is published in the
        tests, so anything it signs is worthless outside development. */
    const PublicKey& testKey() noexcept;
}

} // namespace drenched
