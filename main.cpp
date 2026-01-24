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

namespace ed = ax::NodeEditor;

void Gui() {
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


    }
    ImGui::End();
}

int main(int, char**) {
    // ImmApp handles the setup of HelloImGui, ImGui, Implot, etc.
    HelloImGui::RunnerParams runnerParams;
    runnerParams.callbacks.ShowGui = Gui;
    runnerParams.appWindowParams.windowTitle = "ImGui Bundle Kitchen Sink";
    runnerParams.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    
    // Enable Implot and other components if needed
    ImmApp::AddOnsParams addOnsParams;
    addOnsParams.withImplot = true;
    addOnsParams.withNodeEditor = true;

    ImmApp::Run(runnerParams, addOnsParams);
    return 0;
}

