#include "tui.hpp"
#include <ftxui/component/component.hpp>       
#include <ftxui/component/screen_interactive.hpp> 
#include <ftxui/dom/elements.hpp>              

using namespace ftxui;

std::string run_search_ui(HistoryDB& db) {
    auto screen = ScreenInteractive::Fullscreen();

    // State
    std::string query_text;
    std::string selected_command;
    int selected_index = 0;
    bool success_only = false; // Filter state
    std::vector<SearchResult> results;
    std::vector<std::string> menu_entries; 

    // --- LOGIC HELPER ---
    auto refresh_search = [&] {
        results = db.search(query_text, SearchScope::GLOBAL, "", success_only);
        menu_entries.clear();
        for (const auto& r : results) {
            menu_entries.push_back(r.cmd);
        }
        selected_index = 0; // Reset selection on new search
    };

    // 1. Input Component with Event Handler
    InputOption input_opt;
    input_opt.on_change = refresh_search; // Only search when typing changes!
    
    Component input_field = Input(&query_text, "Type to search...", input_opt);

    // 2. Menu Component
    auto menu_opt = MenuOption();
    menu_opt.on_enter = [&] {
        if (selected_index >= 0 && selected_index < results.size()) {
            selected_command = results[selected_index].cmd;
            screen.ExitLoopClosure()(); 
        }
    };
    Component menu = Menu(&menu_entries, &selected_index, menu_opt);

    // 3. Container
    Component container = Container::Vertical({
        input_field,
        menu
    });

    // 4. Catch global hotkeys (e.g. Ctrl+F for filter)
    container |= CatchEvent([&](Event event) {
        if (event == ftxui::Event::Special({6})) {
            success_only = !success_only;
            refresh_search();
            return true;
        }
        return false;
    });

    // 5. Renderer
    auto renderer = Renderer(container, [&] {
        return vbox({
            hbox({
                text(" BSH History ") | bold | color(Color::Blue),
                text(success_only ? " [Success Only] " : " [All] ") | color(success_only ? Color::Green : Color::Red)
            }),
            separator(),
            hbox({
                text(" > "),
                input_field->Render()
            }) | border,
            vbox({
                menu->Render() | vscroll_indicator | frame | flex
            }) | border
        });
    });

    // Initial load
    refresh_search();

    screen.Loop(renderer);
    return selected_command;
}