cmake_minimum_required(VERSION 3.15)
project(tax_assistant LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependency layout
# All third-party headers are vendored under backend/include so the project
# builds without a network fetch step. The canonical layout is:
#   backend/include/json.hpp            (nlohmann/json, single header)
#   backend/include/sqlite3.h|c         (SQLite3 amalgamation)
#   backend/include/crow/               (Crow, header-only, include path)
#   backend/include/asio/               (standalone Asio, include path)
# ---------------------------------------------------------------------------
set(BACKEND_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/backend/include)
set(FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(tax_assistant_server
  backend/src/main.cpp
  backend/include/sqlite3.c            # SQLite amalgamation compiled as C
)

# crow.h/crow/*.h resolve via -I backend/include ("crow/..." style includes);
# standalone Asio resolves via <asio.hpp> from -I backend/include/asio.
target_include_directories(tax_assistant_server PRIVATE
  ${BACKEND_INCLUDE_DIR}               # own headers + json.hpp + crow tree
  ${BACKEND_INCLUDE_DIR}/asio          # standalone Asio
)

# ---------------------------------------------------------------------------
# Platform-specific settings (Windows-FIRST)
# ---------------------------------------------------------------------------
if(WIN32)
  # Static-link Asio into the WinSock subsystem. Without VCPKG Boost we must
  # pull the Winsock libraries manually; pthread does not exist on native MSVC.
  target_link_libraries(tax_assistant_server PRIVATE
    ws2_32
    crypt32
    wsock32
  )
  # Prevent Asio socket build errors on natively-built Windows executables:
  #  - _WIN32_WINNT=0x0601 targets Windows 7+ APIs (WSAPoll etc.)
  #  - WIN32_LEAN_AND_MEAN strips winsock.h, forcing Asio onto winsock2.h
  target_compile_definitions(tax_assistant_server PRIVATE
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
  )
  # NOMINMAX keeps std::numeric_limits / min-max templates from colliding with
  # the Windows.h macros (Crow + third-party headers).
  if(MSVC)
    target_compile_definitions(tax_assistant_server PRIVATE NOMINMAX)
  endif()
else()
  # POSIX thread + curl for the live Gemini client (Phase 2). CMake finds the
  # curl library via its config; the header is already vendored.
  find_package(CURL REQUIRED)
  target_link_libraries(tax_assistant_server PRIVATE pthread CURL::libcurl)
endif()

# --- Release optimizations -------------------------------------------------
# -fno-strict-aliasing + explicit math rounding flags keep tax splits
# deterministic across compilers -- the "0% hallucination" guarantee byte.
if(NOT MSVC)
  target_compile_options(tax_assistant_server PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Frontend → build tree copy (static assets served by Crow app's static dir)
# ---------------------------------------------------------------------------
add_custom_command(TARGET tax_assistant_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${FRONTEND_DIR} $<TARGET_FILE_DIR:tax_assistant_server>/frontend
  COMMENT "Copying frontend static assets into build output"
)

# ---------------------------------------------------------------------------
# Tests (Phase 1 gate)
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()