cmake_minimum_required(VERSION 3.10)
project(audio-poc)


set(CMAKE_PROJECT_VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17)
project(audio
    DESCRIPTION "Viam module for audio"
    HOMEPAGE_URL https://github.com/oliviamiller/audio-poc
    LANGUAGES CXX
)
add_executable(audio-module
    src/main.cpp
    src/module.cpp
    src/audio_stream.cpp
)


find_package(viam-cpp-sdk REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED)

pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

target_link_libraries(audio-module
PRIVATE Threads::Threads
PRIVATE viam-cpp-sdk::viamsdk
PRIVATE ${PORTAUDIO_LIBRARIES}
PRIVATE Boost::boost
)

target_include_directories(audio-module PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
target_compile_options(audio-module PRIVATE ${PORTAUDIO_CFLAGS_OTHER})

# Manual test executable
# add_executable(test-manual test_manual.cpp)
# target_link_libraries(test-manual
#     PRIVATE Threads::Threads
#     PRIVATE viam-cpp-sdk::viamsdk
#     PRIVATE ${PORTAUDIO_LIBRARIES}
# )
# target_include_directories(test-manual PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
# target_compile_options(test-manual PRIVATE ${PORTAUDIO_CFLAGS_OTHER})

enable_testing()
add_subdirectory(test)
