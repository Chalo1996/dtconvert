#!/bin/bash
# Build script for dtconvert

echo "========================================="
echo "Building dtconvert Document Converter"
echo "========================================="

# Check if make is available
if ! command -v make &> /dev/null; then
    echo "Error: 'make' is not installed. Please install it first."
    echo "On Ubuntu/Debian: sudo apt-get install build-essential"
    echo "On Fedora: sudo dnf install make gcc"
    exit 1
fi

# Check if gcc is available
if ! command -v gcc &> /dev/null; then
    echo "Error: 'gcc' is not installed. Please install it first."
    exit 1
fi

echo ""
echo "Step 1: Cleaning previous build..."
make clean

echo ""
echo "Step 2: Creating sample converters..."
make samples

echo ""
echo "Step 3: Building dtconvert..."
if make all; then
    echo ""
    echo "✅ Build successful!"
    echo ""
    
    # Test the binary
    if [ -f "bin/dtconvert" ]; then
        echo "Binary created: bin/dtconvert"
        echo ""
        echo "Test 1: Version information"
        echo "---------------------------"
        ./bin/dtconvert --version
        
        echo ""
        echo "Test 2: Help information"
        echo "------------------------"
        ./bin/dtconvert --help
        
        echo ""
        echo "Test 3: Create a test file"
        echo "--------------------------"
        TEST_FILE="test_document.txt"
        echo "This is a test document." > "$TEST_FILE"
        echo "Created test file: $TEST_FILE"
        
        echo ""
        echo "Test 4: Try conversion command (will show help since format is missing)"
        echo "-----------------------------------------------------------------------"
        ./bin/dtconvert "$TEST_FILE" --to pdf --verbose || true
        
        echo ""
        echo "========================================="
        echo "Build completed successfully!"
        echo ""
        echo "Usage examples:"
        echo "  ./bin/dtconvert document.docx --to pdf"
        echo "  ./bin/dtconvert --help"
        echo ""
        echo "To install system-wide: sudo make install"
        echo "========================================="
        
        # Clean up test file
        rm -f "$TEST_FILE"
    else
        echo "❌ Error: Binary not created!"
        exit 1
    fi
else
    echo ""
    echo "❌ Build failed!"
    exit 1
fi