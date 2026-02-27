
set(OPENSSL_ROOT_DIR "${CMAKE_SOURCE_DIR}/boringssl")

set(BORINGSSL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/boringssl")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")

if(NOT TARGET crypto)
  message(FATAL_ERROR "Expected vendored BoringSSL target 'crypto' to exist before find_package(OpenSSL).")
endif()

if(NOT TARGET ssl)
  message(FATAL_ERROR "Expected vendored BoringSSL target 'ssl' to exist before find_package(OpenSSL).")
endif()

if(NOT TARGET OpenSSL::Crypto)
  add_library(OpenSSL::Crypto ALIAS crypto)
endif()

if(NOT TARGET OpenSSL::SSL)
  add_library(OpenSSL::SSL ALIAS ssl)
endif()

set(OPENSSL_CRYPTO_LIBRARY OpenSSL::Crypto)
set(OPENSSL_SSL_LIBRARY OpenSSL::SSL)

set(OPENSSL_SSL_LIBRARIES ${OPENSSL_SSL_LIBRARY})
set(OPENSSL_CRYPTO_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY})
set(OPENSSL_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY})

# set(BORINGSSL_INCLUDE_DIR ${OPENSSL_INCLUDE_DIR})
# check_required_components(OpenSSL)

set(OPENSSL_FOUND YES)
set(OpenSSL_VERSION "1.1.1")
set(OpenSSL_FOUND YES)
set(OpenSSL_SSL_FOUND YES)
set(OpenSSL_Crypto_FOUND YES)

message(STATUS "OPENSSL_ROOT_DIR: ${OPENSSL_ROOT_DIR}")
message(STATUS "OPENSSL_INCLUDE_DIR: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_CRYPTO_LIBRARY: ${OPENSSL_CRYPTO_LIBRARY}")
message(STATUS "OPENSSL_SSL_LIBRARY: ${OPENSSL_SSL_LIBRARY}")
