cmake_minimum_required(VERSION 3.20)
project(locallens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(locallens src/main.cpp)
find_package(SQLite3 REQUIRED)
target_link_libraries(locallens PRIVATE SQLite::SQLite3)

find_package(OpenSSL)
if(OpenSSL_FOUND)
  target_compile_definitions(locallens PRIVATE LOCALLENS_USE_OPENSSL=1)
  target_link_libraries(locallens PRIVATE OpenSSL::Crypto)
elseif(APPLE)
  target_compile_definitions(locallens PRIVATE LOCALLENS_USE_COMMONCRYPTO=1)
endif()

target_compile_options(locallens PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME fixture-imports COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-fixtures.sh $<TARGET_FILE:locallens>)
