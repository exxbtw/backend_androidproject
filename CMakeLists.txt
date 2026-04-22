cmake_minimum_required(VERSION 3.14)
project(backend_androidproject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Поиск зависимостей ---
find_package(Threads REQUIRED)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# Поиск ZeroMQ
find_path(ZMQ_INCLUDE_DIRS NAMES zmq.hpp PATHS /opt/homebrew/include /usr/local/include /usr/include)
find_library(ZMQ_LIBRARIES NAMES zmq PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)

# Пути для libpq (PostgreSQL)
set(LIBPQ_INCLUDE_DIR "/usr/local/opt/libpq/include")
set(LIBPQ_LIB "/usr/local/opt/libpq/lib/libpq.dylib")

# --- Сборка сторонних библиотек (ImGui, ImPlot) ---
set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)

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

# --- Сборка основного приложения ---
# Укажи здесь путь к папке, где теперь лежат твои файлы (например, если в examples)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/examples)

add_executable(example_app 
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/database.cpp
    ${SRC_DIR}/map_engine.cpp
    ${SRC_DIR}/telemetry.cpp
    ${SRC_DIR}/gui_app.cpp
)

target_include_directories(example_app PRIVATE 
    ${ZMQ_INCLUDE_DIRS}
    ${SRC_DIR}
    ${LIBPQ_INCLUDE_DIR}
)

target_link_libraries(example_app
    PRIVATE
    imgui
    implot
    ${ZMQ_LIBRARIES}     
    Threads::Threads
    ${LIBPQ_LIB}
    ${CMAKE_DL_LIBS} # Для динамических библиотек, если нужно
)