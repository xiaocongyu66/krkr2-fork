vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Cyan4973/xxHash
    REF "v${VERSION}"
    SHA512 8b5c8b9aad4e869f28310b12cc314037feda81d92f26c23eaecdb35dc65042ca2e65f2e9606033e62a31bcc737a9a950500ffcbdb8677d6ab20e820ea14f2b79
    HEAD_REF dev
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES xxhsum XXHASH_BUILD_XXHSUM
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/cmake_unofficial"
    OPTIONS ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/xxHash)

if("xxhsum" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES xxhsum AUTO_CLEAN)
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_fixup_pkgconfig()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# krkr2 overlay: the engine includes <xxhash/xxhash.h> (matching the
# layout cocos2d-x bundles internally); install the same alias layout.
if(EXISTS "${CURRENT_PACKAGES_DIR}/include/xxhash.h")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include/xxhash")
    file(RENAME "${CURRENT_PACKAGES_DIR}/include/xxhash.h"
                "${CURRENT_PACKAGES_DIR}/include/xxhash/xxhash.h")
endif()
if(EXISTS "${CURRENT_PACKAGES_DIR}/debug/include/xxhash.h")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/include/xxhash")
    file(RENAME "${CURRENT_PACKAGES_DIR}/debug/include/xxhash.h"
                "${CURRENT_PACKAGES_DIR}/debug/include/xxhash/xxhash.h")
endif()
