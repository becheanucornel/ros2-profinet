#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetches p-net v0.2.0 and what it needs, at exact commits, and applies our patches.
# To use your own copies: UPSTREAM=https://github.com/your-account ./fetch.sh
set -eu
cd "$(dirname "$0")"
UPSTREAM=${UPSTREAM:-https://github.com/rtlabs-com}

get() { # repository, directory, commit
    if [ -e "$2/.git" ]; then
        echo "$2 already exists"
        return
    fi
    git clone --no-checkout "$UPSTREAM/$1.git" "$2"
    git -C "$2" checkout --detach "$3"
}

get p-net p-net 3e650720850e1f31f8a419c8aa135a2e81b44ddb
get osal osal 88784fcfff7a1b74e20bdeb71a55c2cdd22b4a59
# A submodule of p-net, cloned directly so that UPSTREAM applies to it too.
get cmake-tools p-net/cmake/tools 45c751c60d75bbad5eb327624c259cc02b6564b2

for patch in "$PWD"/patches/*.patch; do
    if git -C p-net apply --reverse --check "$patch" 2>/dev/null; then
        echo "$(basename "$patch") is already applied"
    else
        git -C p-net apply "$patch"
    fi
done
