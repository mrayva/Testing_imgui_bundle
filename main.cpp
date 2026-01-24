#include "immapp/immapp.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot/implot.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include <vector>
#include <string>
#include <cmath>
#include <reaction/reaction.h>
#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>
#include "table_foo.h"
#include <iostream>
#include <filesystem>

namespace ed = ax::NodeEditor;

static std::vector<std::string> g_FontNames = {"Default Font"};
static int g_GlobalFontIdx = 0;

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

        // sqlpp23 Demo
        if (ImGui::CollapsingHeader("Type-safe SQL (sqlpp23) Example")) {
            static std::unique_ptr<sqlpp::sqlite3::connection> db;
            static std::string last_error;
            static std::vector<std::string> results;

            if (!db) {
                try {
                    sqlpp::sqlite3::connection_config config;
                    config.path_to_database = ":memory:";
                    config.flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
                    db = std::make_unique<sqlpp::sqlite3::connection>(config);

                    // Create table
                    (*db)("CREATE TABLE foo (id BIGINT, name TEXT, has_fun BOOLEAN)");
                } catch (const std::exception& e) {
                    last_error = e.what();
                }
            }

            if (db) {
                if (ImGui::Button("Insert Random Row")) {
                    static int next_id = 1;
                    try {
                        auto name = "User " + std::to_string(next_id);
                        (*db)(insert_into(test_db::foo).set(test_db::foo.Id = next_id++, test_db::foo.Name = name, test_db::foo.HasFun = true));
                    } catch (const std::exception& e) {
                        last_error = e.what();
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Select All Rows")) {
                    results.clear();
                    try {
                        for (const auto& row : (*db)(select(test_db::foo.Id, test_db::foo.Name).from(test_db::foo))) {
                            results.push_back("ID: " + std::to_string(row.Id) + ", Name: " + std::string(row.Name));
                        }
                    } catch (const std::exception& e) {
                        last_error = e.what();
                    }
                }




                if (!last_error.empty()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", last_error.c_str());
                    if (ImGui::Button("Clear Error")) last_error.clear();
                }

                ImGui::Separator();
                ImGui::Text("Results:");
                for (const auto& res : results) {
                    ImGui::Text("- %s", res.c_str());
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Database not initialized: %s", last_error.c_str());
            }
        }

        // FreeType Demo
        if (ImGui::CollapsingHeader("Font Rendering (FreeType) Info")) {
            ImGui::Text("FreeType: ACTIVE");
            if (ImGui::GetIO().Fonts->TexData) {
                ImGui::Text("Font Atlas: %d x %d", ImGui::GetIO().Fonts->TexData->Width, ImGui::GetIO().Fonts->TexData->Height);
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
                        if (ImGui::Selectable(g_FontNames[n].c_str(), is_selected))
                            selected_font = n;

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
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
                const char* modes[] = { "None", "No Hinting", "Light", "Normal", "Mono" };
                if (ImGui::Combo("Hinting Mode", &hinting_mode, modes, IM_ARRAYSIZE(modes))) {
                    // Note: Changing these typically requires rebuilding the font atlas
                    ImGui::GetIO().Fonts->FontLoaderFlags = (1 << hinting_mode); 
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Settings changed. Atlas needs re-build.");
                }
                ImGui::TreePop();
            }
        }




    }
    ImGui::End();

    if (g_GlobalFontIdx < ImGui::GetIO().Fonts->Fonts.Size) {
        ImGui::PopFont();
    }
}


int main(int, char**) {
    // ImmApp handles the setup of HelloImGui, ImGui, Implot, etc.
    HelloImGui::RunnerParams runnerParams;
    runnerParams.callbacks.ShowGui = Gui;
    runnerParams.appWindowParams.windowTitle = "ImGui Bundle Kitchen Sink";
    runnerParams.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    
    // Load professional system fonts
    runnerParams.callbacks.LoadAdditionalFonts = []() {
        auto& io = ImGui::GetIO();
        io.Fonts->AddFontDefault();
        
        std::string root = "/usr/share/fonts/truetype";
        if (std::filesystem::exists(root)) {
            int count = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (count >= 100) break; // Safety cap
                if (entry.path().extension() == ".ttf") {
                    if (io.Fonts->AddFontFromFileTTF(entry.path().string().c_str(), 18.0f)) {
                        g_FontNames.push_back(entry.path().filename().string());
                        count++;
                    }
                }
            }
        }
    };


    // Enable Implot and other components if needed

    ImmApp::AddOnsParams addOnsParams;
    addOnsParams.withImplot = true;
    addOnsParams.withNodeEditor = true;

    ImmApp::Run(runnerParams, addOnsParams);
    return 0;
}

