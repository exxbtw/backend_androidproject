#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <cmath>
#include <mutex>
#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <zmq.hpp>
#include "json.hpp"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

using json = nlohmann::json;

struct LocationData {
    float lat = 0.0f;
    float lon = 0.0f;
    float alt = 0.0f;
    long long timestamp = 0;
    std::mutex mtx;        
    volatile bool is_running; 
};

void run_server(LocationData* loc) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    
    try {
        socket.bind("tcp://*:5566");
    } catch (const std::exception& e) {
        std::cerr << "ZMQ Bind Error: " << e.what() << std::endl;
        return;
    }

    int timeout = 1000; 
    socket.set(zmq::sockopt::rcvtimeo, timeout);
    
    while (loc->is_running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        
        if (!res) continue;

        std::string msg_str(static_cast<char*>(request.data()), request.size());
        
        try {
            auto j = json::parse(msg_str);

            {
                loc->mtx.lock();
                loc->lat = j["latitude"].get<double>();
                loc->lon = j["longitude"].get<double>();
                loc->alt = j["altitude"].get<double>();
                loc->timestamp = j["time"].get<long long>();
                loc->mtx.unlock(); 
            }

            std::ofstream file("location_log.json", std::ios::app);
            file << j.dump() << std::endl;

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        } catch (...) {
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}

int main(int argc, char *argv[]) {
    static LocationData locationInfo;
    locationInfo.is_running = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "Бэкендикс",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui_layout.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    std::thread server_thread(run_server, &locationInfo);

    bool running = true;
    bool show_settings = false;
    float ui_scale = 1.0f;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Smartphone Location");
        if (ImGui::Button("Settings")) {
            show_settings = true;
        }

        float currentLat, currentLon, currentAlt;
        long long currentTime;

        {
            locationInfo.mtx.lock();
            currentLat = locationInfo.lat;
            currentLon = locationInfo.lon;
            currentAlt = locationInfo.alt;
            currentTime = locationInfo.timestamp;
            locationInfo.mtx.unlock();
        }

        ImGui::Text("Latitude:  %.6f", currentLat);
        ImGui::Text("Longitude: %.6f", currentLon);
        ImGui::Text("Altitude:  %.2f m", currentAlt);
        ImGui::Separator();
        ImGui::Text("Last update: %lld", currentTime);

        if (ImGui::Button("Clear Log")) {
            std::ofstream file("location_log.json", std::ios::trunc);
            file.close();
        }
        ImGui::End();

        if (show_settings) {
            ImGui::Begin("Settings", &show_settings);
            ImGui::ShowStyleEditor(); 

            if (ImGui::SliderFloat("UI Scale", &ui_scale, 0.5f, 2.0f)) {
                ImGui::GetIO().FontGlobalScale = ui_scale;
            }

            if (ImGui::Button("Save UI Settings")) {
                ImGui::SaveIniSettingsToDisk("imgui_layout.ini");
            }

            ImGui::Separator();
            ImGui::Text("Server runs on port 5566");
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    locationInfo.is_running = false; 
    if (server_thread.joinable()) {
        server_thread.join();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}