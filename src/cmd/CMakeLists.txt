INCLUDE_DIRECTORIES (
    ${CMAKE_CURRENT_SOURCE_DIR}/base
  ${CMAKE_CURRENT_SOURCE_DIR}/restore
  ${CMAKE_CURRENT_SOURCE_DIR}/kits
  ${CMAKE_CURRENT_SOURCE_DIR}/loginspect
  ${CMAKE_CURRENT_SOURCE_DIR}/../../config
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/sm
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/cmd
)
# to look for generated headers
INCLUDE_DIRECTORIES (
  ${CMAKE_CURRENT_BINARY_DIR}/../../config
  ${CMAKE_CURRENT_BINARY_DIR}/../../src/sm
  ${CMAKE_CURRENT_BINARY_DIR}/../../src/common
)

add_subdirectory(base)
add_subdirectory(restore)
add_subdirectory(kits)
add_subdirectory(loginspect)

add_executable(zapps main.cpp)

set(zapps_LIBS
    # zapps components
    zapps_base
    kits
    restore
    loginspect
    # Zero/Shore libraries
    sm
    common
    # third-party dependencies
    pthread
)

target_link_libraries(zapps ${zapps_LIBS})
