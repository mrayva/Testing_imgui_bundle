#!/bin/bash
# Verification script for DatabaseManager refactoring

echo "=== DatabaseManager Refactoring Verification ==="
echo

echo "1. Checking file structure..."
if [ -d "database" ] && [ -d "database/schemas" ] && [ -d "database/repositories" ]; then
    echo "   ✓ Directory structure correct"
else
    echo "   ✗ Directory structure missing"
    exit 1
fi

echo "2. Checking core files..."
files=(
    "database/database_mode.h"
    "database/database_manager.h"
    "database/schemas/table_foo.h"
    "database/repositories/foo_repository.h"
)
for file in "${files[@]}"; do
    if [ -f "$file" ]; then
        echo "   ✓ $file exists"
    else
        echo "   ✗ $file missing"
        exit 1
    fi
done

echo "3. Checking old files removed..."
if [ ! -f "database_manager.h" ] && [ ! -f "table_foo.h" ]; then
    echo "   ✓ Old files removed from root"
else
    echo "   ✗ Old files still in root"
    exit 1
fi

echo "4. Checking main.cpp includes..."
if grep -q 'database/database_manager.h' main.cpp && \
   grep -q 'database/repositories/foo_repository.h' main.cpp; then
    echo "   ✓ main.cpp includes updated"
else
    echo "   ✗ main.cpp includes incorrect"
    exit 1
fi

echo "5. Checking documentation..."
docs=(
    "database/README.md"
    "database/MIGRATION.md"
    "database/EXAMPLES.md"
    "database/ARCHITECTURE.md"
)
for doc in "${docs[@]}"; do
    if [ -f "$doc" ]; then
        echo "   ✓ $doc exists"
    else
        echo "   ✗ $doc missing"
    fi
done

echo
echo "=== Verification Complete ==="
echo
echo "To build and test:"
echo "  export CC=/usr/bin/gcc-14 CXX=/usr/bin/g++-14"
echo "  rm -rf build && mkdir build && cd build"
echo "  cmake -G Ninja .."
echo "  ninja"
echo "  ./KitchenSinkImgui"
