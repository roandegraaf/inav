#!/bin/bash
set -e

BRANCH="corewing-dual-led-strip"
TARGET="COREWINGF405WINGV2"
TOOLCHAIN_DIR="tools/arm-gnu-toolchain-13.2.rel1/bin"

echo "=== INAV Custom Firmware Update & Build ==="
echo ""

# Make sure we're on the right branch
CURRENT=$(git branch --show-current)
if [ "$CURRENT" != "$BRANCH" ]; then
    echo "Switching to $BRANCH..."
    git checkout "$BRANCH"
fi

# Fetch latest from upstream (iNavFlight/inav)
echo "Fetching latest from upstream..."
git fetch upstream

# Check if there's actually an update available
LOCAL_BASE=$(git merge-base HEAD upstream/master)
UPSTREAM_HEAD=$(git rev-parse upstream/master)

if [ "$LOCAL_BASE" = "$UPSTREAM_HEAD" ]; then
    echo "Already up to date with upstream."
    echo ""
    read -p "No update available. Build anyway? [y/N] " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Nothing to do."
        exit 0
    fi
else
    # Show what's new
    CURRENT_TAG=$(git describe --tags --abbrev=0 "$LOCAL_BASE" 2>/dev/null || echo "unknown")
    LATEST_TAG=$(git describe --tags --abbrev=0 upstream/master 2>/dev/null || echo "unknown")
    NEW_COMMITS=$(git rev-list --count "$LOCAL_BASE"..upstream/master)

    echo ""
    echo "Update available!"
    echo "  Current base: $CURRENT_TAG"
    echo "  Latest:       $LATEST_TAG"
    echo "  New commits:  $NEW_COMMITS"
    echo ""
    read -p "Continue with update and build? [Y/n] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        echo "Aborted."
        exit 0
    fi

    # Rebase our changes on top of upstream master
    echo ""
    echo "Rebasing $BRANCH onto upstream/master..."
    if git rebase upstream/master; then
        echo "Rebase successful!"
    else
        echo ""
        echo "=== REBASE CONFLICT ==="
        echo "There are conflicts that need manual resolution."
        echo "Fix the conflicts, then run:"
        echo "  git rebase --continue"
        echo "  ./update-and-build.sh  (to resume building)"
        exit 1
    fi

    # Push updated branch to your fork
    echo ""
    echo "Pushing updated branch to origin..."
    git push origin "$BRANCH" --force-with-lease
fi

# Build
LATEST_TAG=$(git describe --tags --abbrev=0 upstream/master 2>/dev/null || echo "unknown")
echo ""
echo "Building $TARGET..."
mkdir -p build
cd build
cmake -DTOOLCHAIN=arm-none-eabi .. -Wno-dev 2>/dev/null
make "$TARGET" 2>&1 | tail -15

# Convert to flashable binary
echo ""
echo "Creating flashable binary..."
cd ..
"$TOOLCHAIN_DIR/arm-none-eabi-objcopy" -O binary "build/bin/$TARGET.elf" "build/bin/$TARGET.bin"

echo ""
echo "=== BUILD COMPLETE ==="
echo "Firmware: build/bin/$TARGET.bin"
echo "Based on: $LATEST_TAG"
echo ""
echo "To flash:"
echo "  1. Hold BOOT button, plug USB"
echo "  2. Run: dfu-util -a 0 -s 0x08000000:leave -D build/bin/$TARGET.bin"
