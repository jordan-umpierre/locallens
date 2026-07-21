cmake_minimum_required(VERSION 3.20)
project(locallens VERSION 0.2.0 LANGUAGES CXX)
set(LOCALLENS_RELEASE "0.2.0-rc1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(locallens src/main.cpp)
target_compile_definitions(locallens PRIVATE LOCALLENS_VERSION="${LOCALLENS_RELEASE}")
find_package(SQLite3 REQUIRED)
target_link_libraries(locallens PRIVATE SQLite::SQLite3)

if(APPLE AND EXISTS "/opt/homebrew/opt/openssl@3")
  set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
endif()
find_package(OpenSSL)
if(OpenSSL_FOUND)
  target_compile_definitions(locallens PRIVATE LOCALLENS_USE_OPENSSL=1)
  target_link_libraries(locallens PRIVATE OpenSSL::Crypto)
elseif(APPLE)
  message(FATAL_ERROR "LocalLens backup needs OpenSSL AES-GCM. Install openssl@3 or build on Linux with OpenSSL.")
endif()

target_compile_options(locallens PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME fixture-imports COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-fixtures.sh $<TARGET_FILE:locallens>)

install(TARGETS locallens RUNTIME DESTINATION bin)
install(DIRECTORY packaging/systemd/ DESTINATION lib/systemd/system)
install(DIRECTORY docs/ DESTINATION share/doc/locallens)

set(CPACK_PACKAGE_NAME "locallens")
set(CPACK_PACKAGE_VERSION "${LOCALLENS_RELEASE}")
set(CPACK_GENERATOR "TGZ")
include(CPack)
