# Third-party C++ dependencies, pinned by URL + SHA-256 (see docs/ADR/0003-dependencies.md).
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# nlohmann/json 3.12.0 - MIT. JSON for the JSON-RPC client and project files.
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# LZ4 1.10.0 - BSD-2-Clause. Skyrim SE BSA (v105) compress files as LZ4 frames.
FetchContent_Declare(lz4
  URL https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz
  URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
  SOURCE_SUBDIR build/cmake
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(LZ4_BUILD_CLI OFF CACHE INTERNAL "")
set(LZ4_BUNDLED_MODE ON CACHE INTERNAL "")
FetchContent_MakeAvailable(lz4)

# nifly (ousnius/nifly @ 2026-09-14) - GPL-3.0. NIF reading for the 3D viewer.
FetchContent_Declare(nifly
  URL https://github.com/ousnius/nifly/archive/cca0a770094bb962fb28ea1fec5ea903e68fda8e.tar.gz
  URL_HASH SHA256=b278fb0cfdaffbf4b6c3c304ef7494fbf049ad8a3e64fa5cc239bd74f41efe1f
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(nifly)
# Third-party headers must not trip our /W4 /WX.
set_target_properties(nifly lz4_static PROPERTIES SYSTEM TRUE)

# DirectXTex May 2026 - MIT. CPU DDS decompression avoids backend-specific compressed uploads.
FetchContent_Declare(directxtex
  URL https://github.com/microsoft/DirectXTex/archive/refs/tags/may2026.tar.gz
  URL_HASH SHA256=204ae68a66fb15d87867d836efbac9ae5aa799ca2a1867024d96a8168fa3651f
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(BUILD_TOOLS OFF CACHE INTERNAL "")
set(BUILD_SAMPLE OFF CACHE INTERNAL "")
set(BUILD_DX11 OFF CACHE INTERNAL "")
set(BUILD_DX12 OFF CACHE INTERNAL "")
set(BC_USE_OPENMP OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(directxtex)
set_target_properties(DirectXTex PROPERTIES SYSTEM TRUE)

if(SNS_BUILD_TESTS)
  # Catch2 3.16.0 - BSL-1.0. Unit test framework for the engines.
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz
    URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  set(CATCH_INSTALL_DOCS OFF CACHE INTERNAL "")
  set(CATCH_INSTALL_EXTRAS OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
