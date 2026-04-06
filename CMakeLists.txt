cmake_minimum_required(VERSION 3.14)
project(backend_androidproject LANGUAGES CXX)

set(LIBPQ_INCLUDE_DIR "/usr/local/opt/libpq/include")
set(LIBPQ_LIB_DIR "/usr/local/opt/libpq/lib")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

find_path(ZMQ_INCLUDE_DIRS NAMES zmq.hpp PATHS /opt/homebrew/include /usr/local/include)
find_library(ZMQ_LIBRARIES NAMES zmq PATHS /opt/homebrew/lib /usr/local/lib)

if(NOT ZMQ_LIBRARIES)
    message(FATAL_ERROR "ZeroMQ не найден")
endif()

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(EXAMPLES_DIR ${PROJECT_SOURCE_DIR}/examples)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC ${THIRD_PARTY_DIR}/imgui ${THIRD_PARTY_DIR}/imgui/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL GLEW::GLEW)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PUBLIC imgui)

add_executable(example_app ${EXAMPLES_DIR}/main.cpp)

target_include_directories(example_app PRIVATE 
    ${ZMQ_INCLUDE_DIRS}
    ${EXAMPLES_DIR}
    ${LIBPQ_INCLUDE_DIR}
)

target_link_libraries(example_app
    PRIVATE
    imgui
    implot
    ${ZMQ_LIBRARIES}     
    Threads::Threads
    /usr/local/opt/libpq/lib/libpq.dylib
)