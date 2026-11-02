#!/usr/bin/env bash
# Fails if a Linux binary needs a newer C or C++ runtime than Ubuntu 22.04 has
# (glibc 2.35, and libstdc++ from GCC 12: GLIBCXX_3.4.30, CXXABI_1.3.13).
# Ubuntu 22.04 and Debian 12 are the oldest systems the Linux downloads promise
# (docs/32-bit-and-64-bit.md); a binary built on a newer system picks up newer
# symbol versions without any warning, and then won't load there.
#   bash .github/scripts/check-glibc.sh <Release artefacts folder>
set -euo pipefail

DIR="${1:?artefacts folder}"
LIMITS="GLIBC_2.35 GLIBCXX_3.4.30 CXXABI_1.3.13"

newest()
{
    objdump -T "$1" | grep -o "${2}_[0-9][0-9.]*" | sort -Vu | tail -n 1
}

failed=0
count=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q '^ELF' || continue
    count=$((count + 1))
    line="$f:"
    for limit in $LIMITS; do
        name="${limit%%_*}"
        have=$(newest "$f" "$name")
        line="$line ${have:-no $name}"
        if [ -n "$have" ] && [ "$(printf '%s\n%s\n' "$have" "$limit" | sort -V | tail -n 1)" != "$limit" ]; then
            echo "::error file=$f::needs $have; Ubuntu 22.04 has at most $limit"
            failed=1
        fi
    done
    echo "$line"
done < <(find "$DIR" \( -name '*.so' -o -name '*.clap' -o \( -name Drenched -type f \) \) -print0)

[ "$count" -gt 0 ] || { echo "no binaries found in $DIR"; exit 1; }
exit "$failed"
