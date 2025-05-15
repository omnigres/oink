cmake_minimum_required(VERSION 3.21)

# Define the project name and C++ standard
project(oink LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED)

# Enable C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Let's be as pedantic as we can
add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
)

# Specify the include directory containing your headers
# Replace "include" with the actual path to your include directory
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Boost_INCLUDE_DIRS})

# Add compile options (warnings, etc.) if needed
target_compile_options(${PROJECT_NAME} INTERFACE -Wall -Wextra -Wpedantic)

# Add installation rules for headers
install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
    DESTINATION include
)

# Tests
add_executable(oink_tests tests/oink.cpp tests/tests.cpp)
target_link_libraries(oink_tests ${PROJECT_NAME})
enable_testing()
add_test(NAME oink_tests COMMAND oink_tests)
