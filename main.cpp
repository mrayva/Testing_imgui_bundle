#include <exception>
#include "immapp/immapp.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot/implot.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include <vector>
#include <string>
#include <cmath>
#include <reaction/reaction.h>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sqlpp23/sqlpp23.h>
#include "database/database_manager.h"
#include "database/schemas/table_foo.h"
#include "database/async_table_widget.h"
#include "nats_client.h"

namespace ed = ax::NodeEditor;

static std::vector<std::string> g_FontNames = {"Default Font"};
static int g_GlobalFontIdx = 0;

// Database results (legacy, can be removed if not used elsewhere)
static std::vector<std::string> g_db_results;

// Async Table Widget (Zero-Lock Rendering)
static db::AsyncTableWidget* g_asyncTable = nullptr;
static std::thread g_refreshThread;
static std::atomic<bool> g_refreshRunning{false};
static std::mutex g_refreshMutex;
static std::condition_variable g_refreshCV;

// NATS State
static NatsClient g_natsClient;
static char g_natsUrl[256] = "wss://demo.nats.io:8443";
static char g_natsSubject[256] = "imgui.demo";
static char g_natsMessage[256] = "Hello from ImGui!";
static std::vector<std::string> g_natsLog;

void Gui() {
    auto& io = ImGui::GetIO();
    if (g_GlobalFontIdx < io.Fonts->Fonts.Size) {
        ImGui::PushFont(io.Fonts->Fonts[g_GlobalFontIdx]);
    }

    // Basic ImGui Demo
    if (ImGui::Begin("Kitchen Sink Demo")) {

        ImGui::Text("Welcome to the ImGui Bundle Kitchen Sink!");
        ImGui::Separator();

        static float f = 0.0f;
        ImGui::SliderFloat("Float Slider", &f, 0.0f, 1.0f);

        if (ImGui::Button("Click Me")) {
            // Do something
        }

        ImGui::Separator();

        // ImPlot Demo
        if (ImGui::CollapsingHeader("ImPlot Example")) {
            static std::vector<float> x_data, y_data;
            if (x_data.empty()) {
                for (int i = 0; i < 100; ++i) {
                    x_data.push_back(i * 0.1f);
                    y_data.push_back(std::sin(i * 0.1f));
                }
            }
            if (ImPlot::BeginPlot("Sine Wave")) {
                ImPlot::PlotLine("sin(x)", x_data.data(), y_data.data(), x_data.size());
                ImPlot::EndPlot();
            }
        }

        // Node Editor Demo (Very basic placeholder)
        if (ImGui::CollapsingHeader("Node Editor Example")) {
            ed::SetCurrentEditor(ImmApp::DefaultNodeEditorContext());
            ed::Begin("My Node Editor");
            ed::BeginNode(1);
            ImGui::Text("Node A");
            ed::BeginPin(2, ed::PinKind::Input);
            ImGui::Text("-> In");
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin(3, ed::PinKind::Output);
            ImGui::Text("Out ->");
            ed::EndPin();
            ed::EndNode();
            ed::End();
        }

        // Reaction Demo
        if (ImGui::CollapsingHeader("Reactive Programming (Reaction) Example")) {
            static auto a = reaction::var(10);
            static auto b = reaction::var(20);
            static auto sum = reaction::calc([]() { return a() + b(); });

            ImGui::Text("Reactive variables 'a' and 'b'");
            int val_a = a.get();
            if (ImGui::InputInt("Variable a", &val_a)) {
                a.value(val_a);
            }
            int val_b = b.get();
            if (ImGui::InputInt("Variable b", &val_b)) {
                b.value(val_b);
            }

            ImGui::Separator();
            ImGui::Text("Computed 'sum' (a + b): %d", sum.get());

            if (ImGui::Button("Reset Reaction Variables")) {
                a.value(10);
                b.value(20);
            }
        }

        // Async Table Demo (Zero-Lock + TYPE ERASURE)
        if (ImGui::CollapsingHeader("Async Table Widget (Zero-Lock + Type Erasure) ✓")) {
            if (g_asyncTable) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Type Erasure: No FooRow struct needed!");
                ImGui::Text("- Query returns sqlpp23 result rows directly");
                ImGui::Text("- Typed data stored in Row.userData (std::any)");
                ImGui::Text("- Type-safe sorting on int64/bool/string");
                ImGui::Text("- String conversion only at render time");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Try sorting by ID - it sorts numerically (typed)!");
                ImGui::Text("Background thread updates every 3 seconds");
                ImGui::Separator();

                // Manual refresh button (wakes background thread)
                if (ImGui::Button("Manual Refresh")) {
                    g_refreshCV.notify_one();
                }

                // Render the table (zero locks!)
                g_asyncTable->Render();

            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Async table not initialized");
            }
        }

        ImGui::Separator();

        // sqlpp23 Demo (Direct queries, no repository!)
        if (ImGui::CollapsingHeader("Type-safe SQL (sqlpp23) Example")) {
            if (DatabaseManager::Get().IsInitialized()) {
                static std::string lastError;

                if (ImGui::Button("Insert Random Row")) {
                    static int next_id = 100; // Start higher to avoid collision with seed
                    auto name = "User " + std::to_string(next_id);
                    try {
                        DatabaseManager::Get().GetConnection()(sqlpp::insert_into(test_db::foo)
                                                                   .set(test_db::foo.Id = next_id++,
                                                                        test_db::foo.Name = name,
                                                                        test_db::foo.HasFun = true));
                        lastError.clear();
                    } catch (const std::exception& e) {
                        lastError = e.what();
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Select All Rows")) {
                    try {
                        g_db_results.clear();
                        for (const auto& row : DatabaseManager::Get().GetConnection()(
                                 sqlpp::select(test_db::foo.Id, test_db::foo.Name).from(test_db::foo))) {
                            g_db_results.push_back("ID: " + std::to_string(row.Id) +
                                                   ", Name: " + std::string(row.Name));
                        }
                        lastError.clear();
                    } catch (const std::exception& e) {
                        lastError = e.what();
                    }
                }

                if (!lastError.empty()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", lastError.c_str());
                    if (ImGui::Button("Clear Error")) lastError.clear();
                }

                ImGui::Separator();
                ImGui::Text("Results:");
                for (const auto& res : g_db_results) {
                    ImGui::Text("- %s", res.c_str());
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Database not initialized");
                if (ImGui::Button("Retry Init")) {
                    DatabaseManager& db = DatabaseManager::Get();
                    if (db.Initialize()) {
                        try {
                            db.GetConnection()(
                                "CREATE TABLE IF NOT EXISTS foo (id BIGINT, name TEXT, has_fun BOOLEAN)");
                            db.GetConnection()(sqlpp::insert_into(test_db::foo)
                                                   .set(test_db::foo.Id = 1, test_db::foo.Name = "Initial User",
                                                        test_db::foo.HasFun = true));
                        } catch (...) {}
                    }
                }
            }
        }

        // FreeType Demo
        if (ImGui::CollapsingHeader("Font Rendering (FreeType) Info")) {
            ImGui::Text("FreeType: ACTIVE");
            if (ImGui::GetIO().Fonts->TexData) {
                ImGui::Text("Font Atlas: %d x %d", ImGui::GetIO().Fonts->TexData->Width,
                            ImGui::GetIO().Fonts->TexData->Height);
            }

            ImGui::Separator();
            ImGui::BulletText("Smoothing logic provided by FreeType engine.");
            ImGui::BulletText("Supports complex glyphs and SVG fonts (via plutosvg).");

#ifdef IMGUI_ENABLE_FREETYPE
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "vcpkg-linked FreeType is strictly enabled.");
#else
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "FreeType might be active via default bundle fallback.");
#endif

            if (ImGui::TreeNode("System Font Browser")) {
                static int selected_font = 0;
                ImGui::Text("Detected Fonts: %zu", g_FontNames.size());

                if (ImGui::BeginListBox("##Fonts", ImVec2(-FLT_MIN, 10 * ImGui::GetTextLineHeightWithSpacing()))) {
                    for (int n = 0; n < (int)g_FontNames.size(); n++) {
                        const bool is_selected = (selected_font == n);
                        if (ImGui::Selectable(g_FontNames[n].c_str(), is_selected)) selected_font = n;

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndListBox();
                }

                ImGui::Separator();
                if (selected_font < ImGui::GetIO().Fonts->Fonts.Size) {
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[selected_font]);
                    ImGui::Text("%s", "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.");
                    ImGui::Text("%s", "0123456789 !@#$%^&*()");

                    ImGui::Text("Current Font: %s", g_FontNames[selected_font].c_str());
                    ImGui::PopFont();

                    if (ImGui::Button("Use this as Global UI Font")) {
                        g_GlobalFontIdx = selected_font;
                    }
                }
                ImGui::TreePop();
            }


            if (ImGui::TreeNode("Advanced FreeType Settings")) {

                static int hinting_mode = 2; // Default to Light
                const char* modes[] = {"None", "No Hinting", "Light", "Normal", "Mono"};
                if (ImGui::Combo("Hinting Mode", &hinting_mode, modes, IM_ARRAYSIZE(modes))) {
                    // Note: Changing these typically requires rebuilding the font atlas
                    ImGui::GetIO().Fonts->FontLoaderFlags = (1 << hinting_mode);
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Settings changed. Atlas needs re-build.");
                }
                ImGui::TreePop();
            }
        }


        // NATS Demo
        if (ImGui::CollapsingHeader("NATS Messaging Example")) {
            ImGui::InputText("NATS URL", g_natsUrl, sizeof(g_natsUrl));
            if (ImGui::Button("Connect")) {
                g_natsClient.Connect(g_natsUrl);
            }
            ImGui::SameLine();
            if (ImGui::Button("Disconnect")) {
                g_natsClient.Disconnect();
            }

            ImGui::Text("Status: %s", g_natsClient.GetConnectionStatus().c_str());
            std::string lastErr = g_natsClient.GetLastError();
            if (!lastErr.empty()) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", lastErr.c_str());
            }

            ImGui::Separator();
            ImGui::InputText("Subject", g_natsSubject, sizeof(g_natsSubject));
            if (ImGui::Button("Subscribe")) {
                g_natsClient.Subscribe(g_natsSubject);
                g_natsLog.push_back("Subscribed to " + std::string(g_natsSubject));
            }

            ImGui::Separator();
            ImGui::InputText("Message", g_natsMessage, sizeof(g_natsMessage));
            if (ImGui::Button("Publish")) {
                g_natsClient.Publish(g_natsSubject, g_natsMessage);
                g_natsLog.push_back("Published to " + std::string(g_natsSubject));
            }

            ImGui::Separator();
            ImGui::Text("NATS Log / Messages:");

            // Poll for new messages
            auto newMsgs = g_natsClient.PollMessages();
            for (const auto& m : newMsgs) {
                g_natsLog.push_back("[" + m.subject + "] " + m.data);
            }

            if (ImGui::BeginChild("NatsLog", ImVec2(0, 200), true)) {
                for (const auto& entry : g_natsLog) {
                    ImGui::TextUnformatted(entry.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            if (ImGui::Button("Clear Log")) g_natsLog.clear();
        }
    }
    ImGui::End();

    if (g_GlobalFontIdx < ImGui::GetIO().Fonts->Fonts.Size) {
        ImGui::PopFont();
    }
}


int main(int, char**) {
    // Initialize database (default: memory mode)
    DatabaseManager& g_dbManager = DatabaseManager::Get();
    g_dbManager.Initialize(DatabaseConfig::Memory());

    // Create table and seed data directly (no repository needed!)
    try {
        g_dbManager.GetConnection()("CREATE TABLE IF NOT EXISTS foo (id BIGINT, name TEXT, has_fun BOOLEAN)");

        // Seed initial data using sqlpp23
        g_dbManager.GetConnection()(
            sqlpp::insert_into(test_db::foo)
                .set(test_db::foo.Id = 1, test_db::foo.Name = "Initial User", test_db::foo.HasFun = true));
    } catch (const std::exception& e) {
        // Handle error - will show in UI
    }

    // Setup Async Table Widget
    g_asyncTable = new db::AsyncTableWidget();
    g_asyncTable->AddColumn("ID", 80.0f);
    g_asyncTable->AddColumn("Name", 200.0f);
    g_asyncTable->AddColumn("Has Fun", 100.0f);
    g_asyncTable->EnableFilter(true);
    g_asyncTable->EnableSelection(true);

    // Set refresh callback (Uses sqlpp23 directly with type erasure!)
    // Define a simple struct to hold typed data
    struct FooTypedData {
        int64_t id;
        std::string name;
        bool hasFun;
    };

    g_asyncTable->SetRefreshCallback([](auto& rows) {
        DatabaseManager& db = DatabaseManager::Get();

        // Execute query - get sqlpp23 result
        auto results = db.GetConnection()(sqlpp::select(sqlpp::all_of(test_db::foo)).from(test_db::foo));

        // Convert sqlpp23 rows with TYPE ERASURE (no FooRepository!)
        for (const auto& sqlppRow : results) {
            // Extract typed values from sqlpp23 row
            int64_t id = sqlppRow.Id;
            std::string name{sqlppRow.Name}; // string_view -> string
            bool hasFun = sqlppRow.HasFun;

            // Create typed data holder
            FooTypedData typedData{id, name, hasFun};

            // Create row with BOTH strings (for display) and typed data (for sorting)
            rows.push_back({
                {std::to_string(id), name, hasFun ? "Yes" : "No"}, // columns
                typedData                                          // userData - MANDATORY!
            });
        }
    });

    // NEW: Set typed extractors for type-safe sorting!
    // Extract int64 ID for accurate numeric sorting
    g_asyncTable->SetColumnTypedExtractor(0, [](const db::AsyncTableWidget::Row& row) -> std::any {
        if (!row.userData.has_value()) return {};
        try {
            auto& data = std::any_cast<const FooTypedData&>(row.userData);
            return std::any(data.id);
        } catch (...) {
            return {};
        }
    });

    // Extract string for Name column (enables multi-column sort)
    g_asyncTable->SetColumnTypedExtractor(1, [](const db::AsyncTableWidget::Row& row) -> std::any {
        if (!row.userData.has_value()) return {};
        try {
            auto& data = std::any_cast<const FooTypedData&>(row.userData);
            return std::any(data.name);
        } catch (...) {
            return {};
        }
    });

    // Extract bool for HasFun column
    g_asyncTable->SetColumnTypedExtractor(2, [](const db::AsyncTableWidget::Row& row) -> std::any {
        if (!row.userData.has_value()) return {};
        try {
            auto& data = std::any_cast<const FooTypedData&>(row.userData);
            return std::any(data.hasFun);
        } catch (...) {
            return {};
        }
    });

    // Initial load
    g_asyncTable->Refresh();

    // Start background refresh thread (every 3 seconds, or on manual trigger)
    g_refreshRunning = true;
    g_refreshThread = std::thread([]() {
        while (g_refreshRunning) {
            {
                std::unique_lock<std::mutex> lock(g_refreshMutex);
                g_refreshCV.wait_for(lock, std::chrono::seconds(3));
            }
            if (!g_refreshRunning) break;
            if (g_asyncTable) {
                g_asyncTable->Refresh();
            }
        }
    });

    // ImmApp handles the setup of HelloImGui, ImGui, Implot, etc.
    HelloImGui::RunnerParams runnerParams;
    runnerParams.callbacks.ShowGui = Gui;
    runnerParams.appWindowParams.windowTitle = "ImGui Bundle Kitchen Sink";
    runnerParams.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;

    // Load professional system fonts
    runnerParams.callbacks.LoadAdditionalFonts = []() {
        auto& io = ImGui::GetIO();
        io.Fonts->AddFontDefault();

        std::vector<std::string> fontRoots;
#ifdef _WIN32
        fontRoots.push_back("C:\\Windows\\Fonts");
#elif defined(__APPLE__)
        fontRoots.push_back("/Library/Fonts");
        fontRoots.push_back("/System/Library/Fonts");
        fontRoots.push_back("~/Library/Fonts");
#elif defined(__EMSCRIPTEN__)
        fontRoots.push_back("fonts");
#else
        fontRoots.push_back("/usr/share/fonts/truetype");
        fontRoots.push_back("/usr/share/fonts/opentype");
        fontRoots.push_back("~/.local/share/fonts");
#endif

        int count = 0;
        for (const auto& root : fontRoots) {
            if (std::filesystem::exists(root)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied)) {
                    if (count >= 100) break; // Safety cap
                    auto ext = entry.path().extension().string();
                    if (ext == ".ttf" || ext == ".otf" || ext == ".TTF" || ext == ".OTF") {
                        if (io.Fonts->AddFontFromFileTTF(entry.path().string().c_str(), 18.0f)) {
                            g_FontNames.push_back(entry.path().filename().string());
                            count++;
                        }
                    }
                }
            }
            if (count >= 100) break;
        }
#ifdef __EMSCRIPTEN__
        // Assets fonts will be loaded via AssetExists/LoadFont if we want to be explicit,
        // but scan-based loading is already handled by the logic above if the path is correct.
        // In Emscripten, the assets are preloaded into "/assets"
        std::string assetRoot = "/assets/fonts";
        if (std::filesystem::exists(assetRoot)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot)) {
                if (count >= 100) break;
                auto ext = entry.path().extension().string();
                if (ext == ".ttf" || ext == ".otf" || ext == ".TTF" || ext == ".OTF") {
                    if (io.Fonts->AddFontFromFileTTF(entry.path().string().c_str(), 18.0f)) {
                        g_FontNames.push_back(entry.path().filename().string() + " (WASM Asset)");
                        count++;
                    }
                }
            }
        }
#endif
    };


    // Enable Implot and other components if needed

    ImmApp::AddOnsParams addOnsParams;
    addOnsParams.withImplot = true;
    addOnsParams.withNodeEditor = true;

    ImmApp::Run(runnerParams, addOnsParams);

    // Shutdown: stop background thread and clean up
    g_refreshRunning = false;
    g_refreshCV.notify_one();
    if (g_refreshThread.joinable()) {
        g_refreshThread.join();
    }
    delete g_asyncTable;
    g_asyncTable = nullptr;

    return 0;
}
