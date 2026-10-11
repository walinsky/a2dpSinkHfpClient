#!/bin/bash
# ==============================================================================
# ESP-IDF Component Overview Script
# Generates a comprehensive directory structure with file sizes and line counts
# ==============================================================================

OUTPUT_FILE="component_overview.txt"
COMPONENT_DIR="${1:-.}"  # Default to current directory if not provided

# Colors for terminal output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}Generating component overview for: ${COMPONENT_DIR}${NC}"
echo "Output file: ${OUTPUT_FILE}"
echo ""

# Create header
{
    echo "================================================================================"
    echo "ESP-IDF COMPONENT OVERVIEW"
    echo "Generated: $(date)"
    echo "Component Directory: $(realpath ${COMPONENT_DIR})"
    echo "================================================================================"
    echo ""
    
    # Directory tree structure
    echo "DIRECTORY STRUCTURE:"
    echo "--------------------------------------------------------------------------------"
    tree -L 3 "${COMPONENT_DIR}" 2>/dev/null || find "${COMPONENT_DIR}" -type d | head -20
    echo ""
    
    # File listing with sizes
    echo "FILE LISTING (sorted by size):"
    echo "--------------------------------------------------------------------------------"
    find "${COMPONENT_DIR}" -type f \( \
        -name "*.c" -o \
        -name "*.h" -o \
        -name "*.cpp" -o \
        -name "*.hpp" -o \
        -name "CMakeLists.txt" -o \
        -name "*.cmake" -o \
        -name "Kconfig" \
    \) -exec ls -lh {} \; | awk '{printf "%-8s %s\n", $5, $9}' | sort -hr
    echo ""
    
    # Line counts per file
    echo "LINE COUNTS PER FILE:"
    echo "--------------------------------------------------------------------------------"
    find "${COMPONENT_DIR}" -type f \( \
        -name "*.c" -o \
        -name "*.h" -o \
        -name "*.cpp" -o \
        -name "*.hpp" \
    \) -exec wc -l {} \; | sort -rn
    echo ""
    
    # Summary statistics
    echo "SUMMARY STATISTICS:"
    echo "--------------------------------------------------------------------------------"
    
    # Count files by type
    echo "File counts:"
    echo "  C files (.c):       $(find "${COMPONENT_DIR}" -name "*.c" | wc -l)"
    echo "  Header files (.h):  $(find "${COMPONENT_DIR}" -name "*.h" | wc -l)"
    echo "  C++ files (.cpp):   $(find "${COMPONENT_DIR}" -name "*.cpp" | wc -l)"
    echo "  C++ headers (.hpp): $(find "${COMPONENT_DIR}" -name "*.hpp" | wc -l)"
    echo ""
    
    # Total lines of code
    total_lines=$(find "${COMPONENT_DIR}" -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -exec wc -l {} \; | awk '{sum += $1} END {print sum}')
    echo "Total lines of code: ${total_lines}"
    echo ""
    
    # Component dependencies (from CMakeLists.txt)
    echo "COMPONENT DEPENDENCIES:"
    echo "--------------------------------------------------------------------------------"
    if [ -f "${COMPONENT_DIR}/CMakeLists.txt" ]; then
        grep -E "(REQUIRES|PRIV_REQUIRES)" "${COMPONENT_DIR}/CMakeLists.txt" || echo "No dependencies found"
    else
        echo "No CMakeLists.txt found"
    fi
    echo ""
    
    # Configuration options (from Kconfig)
    echo "CONFIGURATION OPTIONS (Kconfig):"
    echo "--------------------------------------------------------------------------------"
    if [ -f "${COMPONENT_DIR}/Kconfig" ]; then
        grep -E "^config|^menuconfig" "${COMPONENT_DIR}/Kconfig" || echo "No config options found"
    else
        echo "No Kconfig found"
    fi
    echo ""
    
    # Memory allocations (heap/stack analysis)
    echo "MEMORY ALLOCATIONS (malloc/xTaskCreate/ringbuffer):"
    echo "--------------------------------------------------------------------------------"
    find "${COMPONENT_DIR}" -type f -name "*.c" -exec grep -Hn "malloc\|xTaskCreate\|xRingbufferCreate" {} \; | head -20
    echo ""
    
    # Buffer sizes and defines
    echo "IMPORTANT DEFINES (BUFFER SIZES):"
    echo "--------------------------------------------------------------------------------"
    find "${COMPONENT_DIR}" -type f \( -name "*.c" -o -name "*.h" \) -exec grep -Hn "#define.*SIZE\|#define.*BUF\|#define.*STACK" {} \; | head -30
    echo ""
    
} > "${OUTPUT_FILE}"

echo -e "${GREEN}✓ Overview generated successfully!${NC}"
echo -e "${BLUE}View with: cat ${OUTPUT_FILE}${NC}"
echo ""

# Display first 50 lines as preview
echo "Preview (first 50 lines):"
echo "--------------------------------------------------------------------------------"
head -50 "${OUTPUT_FILE}"
echo "..."
echo "--------------------------------------------------------------------------------"
echo -e "${BLUE}Full output saved to: ${OUTPUT_FILE}${NC}"
