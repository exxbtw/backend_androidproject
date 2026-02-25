cmake_minimum_required(VERSION 3.14)
project(backend_androidproject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Поддержка потоков
find_package(Threads REQUIRED)

# 2. УМНЫЙ ПОИСК ZeroMQ (специально для macOS)
find_path(ZMQ_INCLUDE_DIRS NAMES zmq.hpp PATHS /opt/homebrew/include /usr/local/include)
find_library(ZMQ_LIBRARIES NAMES zmq PATHS /opt/homebrew/lib /usr/local/lib)

if(NOT ZMQ_LIBRARIES)
    message(FATAL_ERROR "ZeroMQ не найден! Выполни в терминале: brew install zeromq cppzmq")
endif()

# 3. Пути к папкам
set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(EXAMPLES_DIR ${PROJECT_SOURCE_DIR}/examples)

# 4. Поиск графических библиотек
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# --- Сборка ImGui ---
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

# --- Сборка ImPlot ---
add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PUBLIC imgui)

# --- Сборка основного приложения ---
add_executable(example_app ${EXAMPLES_DIR}/main.cpp)

# Добавляем пути к заголовкам (чтобы main.cpp видел zmq.hpp и json.hpp)
target_include_directories(example_app PRIVATE 
    ${ZMQ_INCLUDE_DIRS}
    ${EXAMPLES_DIR}    # Здесь должен лежать json.hpp
)

# Финальная склейка
target_link_libraries(example_app
    PRIVATE
    imgui
    implot
    ${ZMQ_LIBRARIES}     # Путь к файлу libzmq.dylib
    Threads::Threads     # Библиотека для std::thread
)