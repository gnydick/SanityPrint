#!/bin/bash

set -e
set -o pipefail

while getopts ":dpa:snt:xbc:yh" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    y )
        export PROCESS_SYMBOLS="1"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64)"
        echo "   -s: Build slicer only"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 11.3"
        echo "   -x: Use Ninja CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -y: Process debug symbols (generate dSYM, Breakpad symbols, and strip binary)"
        exit 0
        ;;
    * )
        ;;
  esac
done

# Set defaults

if [ -z "$ARCH" ]; then
  ARCH="$(uname -m)"
  export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="11.3"
fi

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo

# if which -s brew; then
# 	brew --prefix libiconv
# 	brew --prefix zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/
# elif which -s port; then
# 	port install libiconv
# 	port install zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:/opt/local/lib
# else
# 	echo "Need either brew or macports to successfully build deps"
# 	exit 1
# fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_BUILD_DIR="$PROJECT_DIR/build_check_$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"
DEPS_BUILD_DIR="$DEPS_DIR/build_$ARCH"
DEPS="$DEPS_BUILD_DIR/dep_$ARCH"

# 读取环境变量 MY_DIR
DEPS_PATH=$DEPS_ENV_DIR
if [ -z "${DEPS_PATH}" ]; then
    echo "env ${DEPS_PATH} is empty."
else
    DEPS=$DEPS_PATH
fi
echo "=====DEPS=====: $DEPS"

# Fix for Multi-config generators
if [ "$SLICER_CMAKE_GENERATOR" == "Xcode" ]; then
    export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"
else
    export BUILD_DIR_CONFIG_SUBDIR=""
fi

function build_deps() {
    echo "Building deps..."
    (
        set -x
        mkdir -p "$DEPS_BUILD_DIR"
        cd "$DEPS_BUILD_DIR"
        if [ "1." != "$BUILD_ONLY". ]; then
            cmake .. \
                -G "${DEPS_CMAKE_GENERATOR}" \
                -DDESTDIR="$DEPS" \
                -DOPENSSL_ARCH="darwin64-${ARCH}-cc" \
                -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                -DCMAKE_OSX_ARCHITECTURES:STRING="${ARCH}" \
                -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
        fi
        cmake --build . --config "$BUILD_CONFIG" --target deps
    )
}

# 生成和处理 dSYM 调试符号 (适配 build_release_macos.sh)
function process_debug_symbols() {
    echo "Processing debug symbols..."
    
    # 对于 build_release_macos.sh，应用名称固定为 SanityPrint
    local app_name="SanityPrint"
    local app_path="$PROJECT_BUILD_DIR/src$BUILD_DIR_CONFIG_SUBDIR/$app_name.app"
    local binary_path="$app_path/Contents/MacOS/$app_name"
    local symbols_dir="$PROJECT_BUILD_DIR/symbols"
    
    # 检查二进制文件是否存在
    if [ ! -f "$binary_path" ]; then
        echo "Error: Binary file not found at $binary_path"
        echo "Available files in src directory:"
        find "$PROJECT_BUILD_DIR/src$BUILD_DIR_CONFIG_SUBDIR" -name "*.app" -type d 2>/dev/null || echo "No .app bundles found"
        return 1
    fi
    
    echo "Found binary: $binary_path"
    
    # 进入二进制文件所在目录
    cd "$(dirname "$binary_path")"
    
    # 步骤1: 生成 dSYM 文件
    echo "Step 1: Generating dSYM file..."
    if dsymutil "$app_name" -o "$app_name.dSYM"; then
        echo "✓ dSYM file generated successfully: $app_name.dSYM"
    else
        echo "✗ Failed to generate dSYM file"
        return 1
    fi
    
    # 步骤2: 生成 Breakpad 符号文件
    echo "Step 2: Generating Breakpad symbol file..."
    if dump_syms "$app_name.dSYM" > "$app_name.sym"; then
        echo "✓ Breakpad symbol file generated: $app_name.sym"
    else
        echo "✗ Failed to generate Breakpad symbol file"
        return 1
    fi
    
    # 步骤3: 创建符号目录结构
    echo "Step 3: Creating symbol directory structure..."
    local symbol_id=$(head -n 1 "$app_name.sym" | awk '{print $4}')
    if [ -z "$symbol_id" ]; then
        echo "✗ Failed to extract symbol ID from .sym file"
        return 1
    fi
    
    echo "Symbol ID: $symbol_id"
    
    # 创建符号目录并复制文件
    local target_dir="symbols/$app_name/$symbol_id"
    mkdir -p "$target_dir"
    cp "$app_name.sym" "$target_dir/$app_name.sym"
    echo "✓ Symbol file copied to: $target_dir/$app_name.sym"
    
    # 步骤4: 剥离二进制文件中的调试信息
    echo "Step 4: Stripping debug symbols from binary..."
    local original_size=$(stat -f%z "$app_name" 2>/dev/null || echo "0")
    
    if strip -S "$app_name"; then
        local stripped_size=$(stat -f%z "$app_name" 2>/dev/null || echo "0")
        echo "✓ Debug symbols stripped from binary"
        echo "  Original size: $(numfmt --to=iec $original_size 2>/dev/null || echo "${original_size} bytes")"
        echo "  Stripped size: $(numfmt --to=iec $stripped_size 2>/dev/null || echo "${stripped_size} bytes")"
        
        if [ "$original_size" -gt "$stripped_size" ]; then
            local saved_bytes=$((original_size - stripped_size))
            echo "  Space saved: $(numfmt --to=iec $saved_bytes 2>/dev/null || echo "${saved_bytes} bytes")"
        fi
    else
        echo "✗ Failed to strip debug symbols"
        return 1
    fi
    
    # 移动符号文件到项目构建目录
    if [ -d "symbols" ]; then
        mv "symbols" "$symbols_dir"
        echo "✓ Symbols moved to: $symbols_dir"
    fi
    
    # 移动 dSYM 文件到项目构建目录
    if [ -d "$app_name.dSYM" ]; then
        mv "$app_name.dSYM" "$PROJECT_BUILD_DIR/$app_name.dSYM"
        echo "✓ dSYM moved to: $PROJECT_BUILD_DIR/$app_name.dSYM"
    fi
    
    echo "✓ Debug symbol processing completed successfully!"
    echo ""
    echo "Generated files:"
    echo "  - dSYM: $PROJECT_BUILD_DIR/$app_name.dSYM"
    echo "  - Symbols: $symbols_dir"
    echo "  - Stripped binary: $binary_path"
    echo ""
    echo "Build directory: $PROJECT_BUILD_DIR (build_check_$ARCH)"
    
    return 0
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        mkdir -p "$DEPS"
        cd "$DEPS_BUILD_DIR"
        tar -zcvf "dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "dep_$ARCH"
    )
}

function build_slicer() {
    echo "Building slicer..."
    (
        set -x
        mkdir -p "$PROJECT_BUILD_DIR"
        cd "$PROJECT_BUILD_DIR"
        if [ "1." != "$BUILD_ONLY". ]; then
            cmake .. \
                -G "${SLICER_CMAKE_GENERATOR}" \
                -DBBL_RELEASE_TO_PUBLIC=1 \
                -DUPDATE_ONLINE_MACHINES=1 \
                -DGENERATE_ORCA_HEADER=0 \
                -DCMAKE_PREFIX_PATH="$DEPS/usr/local" \
                -DCMAKE_INSTALL_PREFIX="$PWD/SanityPrint" \
                -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                -DCMAKE_MACOSX_RPATH=ON \
                -DCMAKE_INSTALL_RPATH="${DEPS}/usr/local" \
                -DCMAKE_MACOSX_BUNDLE=ON \
                -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
                -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                -DENABLE_BREAKPAD=ON
        fi
        cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
    )

    echo "Verify localization with gettext..."
    (
        cd "$PROJECT_DIR"
        ./run_gettext.sh
    )

    # echo "Fix macOS app package..."
    # (
    #     cd "$PROJECT_BUILD_DIR"
    #     mkdir -p SanityPrint
    #     cd SanityPrint
    #     # remove previously built app
    #     rm -rf ./SanityPrint.app
    #     # fully copy newly built app
    #     cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/SanityPrint.app" ./SanityPrint.app
    #     # fix resources
    #     resources_path=$(readlink ./SanityPrint.app/Contents/Resources)
    #     rm ./SanityPrint.app/Contents/Resources
    #     cp -R "$resources_path" ./SanityPrint.app/Contents/Resources
    #     # delete .DS_Store file
    #     find ./SanityPrint.app/ -name '.DS_Store' -delete
    # )

    # extract version
    # export ver=$(grep '^#define SANITYPRINT_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
    # ver="_V${ver//\"}"
    # echo $PWD
    # if [ "1." != "$NIGHTLY_BUILD". ];
    # then
    #     ver=${ver}_dev
    # fi

    # zip -FSr SanityPrint${ver}_Mac_${ARCH}.zip SanityPrint.app
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        # Process debug symbols if requested
        if [ "$PROCESS_SYMBOLS" = "1" ]; then
            echo "Processing debug symbols..."
            process_debug_symbols
        fi
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        # Process debug symbols if requested
        if [ "$PROCESS_SYMBOLS" = "1" ]; then
            echo "Processing debug symbols..."
            process_debug_symbols
        fi
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, all."
        exit 1
        ;;
esac

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi
