cmake_minimum_required(VERSION 3.14)
project(miniaudio C)

set(MINIAUDIO_NO_RUNTIME_LINKING ON)
set(COMPILE_DEFINES MA_NO_RUNTIME_LINKING)

# 1. Find the backend libraries DIRECTLY in your main CMakeLists.txt 
#    (or ensure they are defined where the targets live)
# find_package(PkgConfig REQUIRED)

# Find ALSA
# pkg_check_modules(PC_ALSA REQUIRED alsa)
find_library(ALSA_LIBRARY NAMES asound HINTS ${PC_ALSA_LIBRARY_DIRS})
find_path(ALSA_INCLUDE_DIR NAMES alsa/asoundlib.h HINTS ${PC_ALSA_INCLUDE_DIRS})

# Find PulseAudio
find_package(PulseAudio REQUIRED)

# Find JACK
# pkg_check_modules(PC_JACK REQUIRED jack)
find_library(JACK_LIBRARY NAMES jack HINTS ${PC_JACK_LIBRARY_DIRS})
find_path(JACK_INCLUDE_DIR NAMES jack/jack.h HINTS ${PC_JACK_INCLUDE_DIRS})

# 2. Create the core static library and link dependencies as PUBLIC
#    (PUBLIC means any target linking to miniaudio_core will ALSO inherit these libraries and headers automatically!)
add_library(miniaudio_core STATIC miniaudio.c)
target_include_directories(miniaudio_core PUBLIC 
    ./miniaudio 
    ./include 
    ${ALSA_INCLUDE_DIR} 
    ${PULSEAUDIO_INCLUDE_DIR} 
    ${JACK_INCLUDE_DIR}
)
target_compile_definitions(miniaudio_core PRIVATE ${COMPILE_DEFINES})

target_link_libraries(miniaudio_core PUBLIC 
    ${ALSA_LIBRARY}
    ${PULSEAUDIO_LIBRARY}
    ${JACK_LIBRARY}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio_core PUBLIC pthread m dl)
endif()

# 3. Node.js module target
add_library(miniaudio MODULE miniaudio.c)
set_target_properties(miniaudio PROPERTIES
    PREFIX ""
    SUFFIX ".node"
)
target_link_libraries(miniaudio PRIVATE miniaudio_core)
