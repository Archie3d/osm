#include "result.h"
#include "osm.h"
#include "map_view.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
class Viewer
{
public:
    ~Viewer()
    {
        if (m_imguiRenderer) {
            ImGui_ImplSDLRenderer3_Shutdown();
        }
        if (m_imguiPlatform) {
            ImGui_ImplSDL3_Shutdown();
        }
        if (m_imguiContext) {
            ImGui::DestroyContext();
        }
        SDL_DestroyTexture(m_texture);
        SDL_DestroyRenderer(m_renderer);
        SDL_DestroyWindow(m_window);
        if (m_sdlInitialized) {
            SDL_Quit();
        }
    }

    osm::Result<void> run(osm::Map& map, const char* filePath, bool smokeTest, const char* snapshotPath)
    {
        constexpr int imageWidth = 1000;
        constexpr int imageHeight = 700;
        osm::MapView view(map.getRegion());
        view.resize(imageWidth, imageHeight);
        BLImage image;
        auto rendered = renderImage(map, view.getRegion(), image, imageWidth, imageHeight);
        if (!rendered) {
            return rendered;
        }
        if (snapshotPath) {
            if (image.write_to_file(snapshotPath) != BL_SUCCESS) {
                return std::unexpected("Cannot write map snapshot");
            }
            return {};
        }

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return sdlError("Initialize video");
        }
        m_sdlInitialized = true;
        m_window = SDL_CreateWindow("OpenStreetMap viewer", imageWidth, imageHeight, SDL_WINDOW_RESIZABLE);
        if (!m_window) {
            return sdlError("Create window");
        }
        m_renderer = SDL_CreateRenderer(m_window, nullptr);
        if (!m_renderer) {
            return sdlError("Create renderer");
        }
        SDL_SetRenderVSync(m_renderer, 1);
        auto uploaded = uploadImage(image);
        if (!uploaded) {
            return uploaded;
        }
        IMGUI_CHECKVERSION();
        m_imguiContext = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        m_imguiPlatform = ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer);
        if (!m_imguiPlatform) {
            return std::unexpected("Cannot initialize ImGui SDL3 backend");
        }
        m_imguiRenderer = ImGui_ImplSDLRenderer3_Init(m_renderer);
        if (!m_imguiRenderer) {
            return std::unexpected("Cannot initialize ImGui renderer backend");
        }

        if (smokeTest) {
            ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        }
        ImVec2 smokeCenter{};
        ImVec2 smokeReset{};
        osm::Map::Coordinate smokeAnchor{};
        const double initialWidth = view.getRegion().max_lon - view.getRegion().min_lon;
        bool redraw = false;
        bool done = false;
        int frames = 0;
        while (!done) {
            if (smokeTest && (frames == 7 || frames == 9)) {
                if (!SDL_SetWindowSize(m_window, frames == 7 ? 600 : 1200, frames == 7 ? 900 : 500)) {
                    return sdlError("Resize smoke-test window");
                }
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT
                    || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_window))
                    || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    done = true;
                }
            }
            if (done) {
                break;
            }
            if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(20);
                continue;
            }
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            // Exercise the same ImGui input path in the headless smoke test:
            // wheel zoom, held-button drag, release, and Reset view click.
            if (smokeTest && frames > 0) {
                auto& io = ImGui::GetIO();
                const ImVec2 pointer = frames >= 5 ? smokeReset
                    : ImVec2(smokeCenter.x + (frames >= 3 ? 40 : 0), smokeCenter.y + (frames >= 3 ? 20 : 0));
                io.AddMousePosEvent(pointer.x, pointer.y);
                if (frames == 1) {
                    io.AddMouseWheelEvent(0, 1);
                }
                if (frames == 2 || frames == 5) {
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                }
                if (frames == 4 || frames == 6) {
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                }
            }
            ImGui::NewFrame();
            const auto* viewport = ImGui::GetMainViewport();
            const ImVec2 canvasStart = viewport->Pos;
            const ImVec2 displaySize = viewport->Size;
            const int renderWidth = std::max(1, static_cast<int>(displaySize.x));
            const int renderHeight = std::max(1, static_cast<int>(displaySize.y));
            redraw |= view.resize(renderWidth, renderHeight);
            const auto& io = ImGui::GetIO();
            const double mouseX = (io.MousePos.x - canvasStart.x) / displaySize.x;
            const double mouseY = (io.MousePos.y - canvasStart.y) / displaySize.y;
            smokeCenter = ImVec2(canvasStart.x + displaySize.x / 2, canvasStart.y + displaySize.y / 2);

            ImGui::SetNextWindowPos(canvasStart);
            ImGui::SetNextWindowSize(displaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::Begin("Map", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::InvisibleButton("Map canvas", displaySize, ImGuiButtonFlags_MouseButtonLeft);
            const bool hovered = ImGui::IsItemHovered();
            if (smokeTest && frames == 1) {
                smokeAnchor = view.coordinateAt(mouseX, mouseY);
            }
            if (ImGui::IsItemActive() && !ImGui::IsItemActivated() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
                redraw |= view.pan(io.MouseDelta.x / displaySize.x, io.MouseDelta.y / displaySize.y);
            }
            if (hovered && io.MouseWheel != 0) {
                redraw |= view.zoom(std::pow(1.25, std::clamp(io.MouseWheel, -20.0f, 20.0f)), mouseX, mouseY);
            }
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            // Controls float over the map instead of reserving strips of canvas.
            ImGui::SetNextWindowPos(ImVec2(canvasStart.x + 10, canvasStart.y + 10));
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(std::max(1.0f, displaySize.x - 20), displaySize.y));
            ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            if (ImGui::Button("+")) {
                redraw |= view.zoom(1.5);
            }
            ImGui::SameLine();
            if (ImGui::Button("-")) {
                redraw |= view.zoom(1 / 1.5);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset view")) {
                redraw |= view.reset();
            }
            const auto resetMin = ImGui::GetItemRectMin();
            const auto resetMax = ImGui::GetItemRectMax();
            smokeReset = ImVec2((resetMin.x + resetMax.x) / 2, (resetMin.y + resetMax.y) / 2);
            if (ImGui::IsWindowHovered()) {
                ImGui::SetTooltip("%s\nDrag to pan | Scroll to zoom | Esc to close", filePath);
            }
            ImGui::End();

            const auto mouseCoordinate = view.coordinateAt(mouseX, mouseY);
            ImGui::SetNextWindowPos(ImVec2(canvasStart.x + 10, canvasStart.y + displaySize.y - 10), ImGuiCond_Always, ImVec2(0, 1));
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::Begin("Coordinates", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
            if (hovered) {
                ImGui::Text("Longitude: %.6f   Latitude: %.6f", mouseCoordinate.longitude, mouseCoordinate.latitude);
            } else {
                ImGui::TextUnformatted("Longitude: --   Latitude: --");
            }
            ImGui::End();

            bool smokeValid = true;
            if (smokeTest && (frames == 1 || frames == 3)) {
                smokeValid = hovered && view.getRegion().max_lon - view.getRegion().min_lon < initialWidth
                    && std::abs(mouseCoordinate.longitude - smokeAnchor.longitude) < initialWidth * 1e-5
                    && std::abs(mouseCoordinate.latitude - smokeAnchor.latitude) < initialWidth * 1e-5;
            }
            if (smokeTest && frames == 6) {
                osm::MapView fitted(map.getRegion());
                fitted.resize(renderWidth, renderHeight);
                const auto& expected = fitted.getRegion();
                smokeValid = view.getRegion().min_lon == expected.min_lon && view.getRegion().max_lon == expected.max_lon
                    && view.getRegion().min_lat == expected.min_lat && view.getRegion().max_lat == expected.max_lat;
            }
            if (smokeTest && (frames == 8 || frames == 10)) {
                const auto center = view.coordinateAt(0.5, 0.5);
                const auto& initial = map.getRegion();
                smokeValid = renderWidth == (frames == 8 ? 600 : 1200) && renderHeight == (frames == 8 ? 900 : 500)
                    && std::abs(center.longitude - (initial.min_lon + initial.max_lon) / 2) < 1e-8
                    && std::abs(center.latitude - (initial.min_lat + initial.max_lat) / 2) < 1e-8;
            }
            if (!smokeValid) {
                ImGui::EndFrame();
                return std::unexpected("Navigation smoke test failed at frame " + std::to_string(frames));
            }
            if (redraw) {
                auto result = renderImage(map, view.getRegion(), image, renderWidth, renderHeight);
                if (result) {
                    result = uploadImage(image);
                }
                if (!result) {
                    ImGui::EndFrame();
                    return result;
                }
                redraw = false;
            }
            ImGui::Render();
            if (!SDL_SetRenderDrawColor(m_renderer, 25, 25, 25, 255) || !SDL_RenderClear(m_renderer)) {
                return sdlError("Clear window");
            }
            if (!SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr)) {
                return sdlError("Draw map background");
            }
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
            if (smokeTest && (frames == 8 || frames == 10)) {
                auto verified = verifyCanvas(image);
                if (!verified) {
                    return verified;
                }
            }
            if (!SDL_RenderPresent(m_renderer)) {
                return sdlError("Present map");
            }
            if (smokeTest && ++frames == 11) {
                done = true;
            }
            SDL_Delay(10);
        }
        return {};
    }

private:
    osm::Result<void> renderImage(osm::Map& map, const osm::Map::Region& region, BLImage& image, int width, int height)
    {
        // @note Blend2D will not reallocate the image if it already of the correct size and format.
        if (image.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return std::unexpected("Cannot allocate map image");
        }

        BLContext context;
        if (context.begin(image) != BL_SUCCESS || context.fill_all(BLRgba32(0xFFF4F1E8)) != BL_SUCCESS) {
            return std::unexpected("Cannot initialize map image");
        }
        auto rendered = map.render(context, region);
        if (!rendered) {
            return rendered;
        }
        if (context.end() != BL_SUCCESS) {
            return std::unexpected("Cannot finish map image");
        }
        return {};
    }

    osm::Result<void> uploadImage(const BLImage& image)
    {
        BLImageData data;
        if (image.get_data(&data) != BL_SUCCESS || data.stride <= 0
            || data.stride > std::numeric_limits<int>::max()) {
            return std::unexpected("Cannot access map image pixels");
        }
        if (!m_texture || m_textureWidth != image.width() || m_textureHeight != image.height()) {
            SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STATIC, image.width(), image.height());
            if (!texture) {
                return sdlError("Create map texture");
            }
            SDL_DestroyTexture(m_texture);
            m_texture = texture;
            m_textureWidth = image.width();
            m_textureHeight = image.height();
        }
        if (!SDL_UpdateTexture(m_texture, nullptr, data.pixel_data, static_cast<int>(data.stride))) {
            return sdlError("Upload map texture");
        }
        return {};
    }

    osm::Result<void> verifyCanvas(const BLImage& image)
    {
        // Read the actual SDL output, ensuring resizing left no letterbox bars
        // or uncovered margins at any corner of the window.
        SDL_Surface* surface = SDL_RenderReadPixels(m_renderer, nullptr);
        if (!surface) {
            return sdlError("Read smoke-test pixels");
        }
        BLImageData data;
        bool valid = image.get_data(&data) == BL_SUCCESS && surface->w == image.width() && surface->h == image.height();
        if (valid) {
            for (int y : {0, image.height() - 1}) {
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(data.pixel_data) + y * data.stride);
                for (int x : {0, image.width() - 1}) {
                    Uint8 r{}, g{}, b{}, a{};
                    valid &= SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
                    valid &= row[x] == ((uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b);
                }
            }
        }
        SDL_DestroySurface(surface);
        if (!valid) {
            return std::unexpected("Map does not cover resized SDL output");
        }
        return {};
    }

    osm::Result<void> sdlError(const char* operation)
    {
        return std::unexpected(std::string(operation) + ": " + SDL_GetError());
    }

    SDL_Window* m_window{};
    SDL_Renderer* m_renderer{};
    SDL_Texture* m_texture{};
    int m_textureWidth{};
    int m_textureHeight{};
    ImGuiContext* m_imguiContext{};
    bool m_sdlInitialized{};
    bool m_imguiPlatform{};
    bool m_imguiRenderer{};
};

} // namespace

int main(int argc, char* argv[])
{
    const bool smokeTest = argc == 3 && std::string_view(argv[2]) == "--smoke-test";
    const bool snapshot = argc == 4 && std::string_view(argv[2]) == "--snapshot";
    if (argc != 2 && !smokeTest && !snapshot) {
        std::cerr << "Usage: viewer <map.osm> [--smoke-test | --snapshot image.png]\n";
        return 1;
    }
    try {
        auto map = osm::Map::load(argv[1]);
        if (!map) {
            std::cerr << map.error() << '\n';
            return 1;
        }
        for (const auto& warning : map->getWarnings()) {
            std::cerr << "Warning: " << warning << '\n';
        }
        Viewer viewer;
        auto result = viewer.run(*map, argv[1], smokeTest, snapshot ? argv[3] : nullptr);
        if (!result) {
            std::cerr << result.error() << '\n';
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "Viewer failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
