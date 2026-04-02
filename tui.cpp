#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "search.hpp"
#include "audio.hpp"

// remove these when move format helpers
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

std::string format_time(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }

    int total_seconds = static_cast<int>(seconds);
    int minutes = total_seconds / 60;
    int remaining_seconds = total_seconds % 60;

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << minutes
        << ":"
        << std::setw(2) << std::setfill('0') << remaining_seconds;
    return oss.str();
}

float compute_progress(double current_seconds, double total_seconds) {
    if (total_seconds <= 0.0) {
        return 0.0f;
    }

    double ratio = current_seconds / total_seconds;
    ratio = std::clamp(ratio, 0.0, 1.0);
    return static_cast<float>(ratio);
}

class TUI {
public:
    TUI()
        : screen_(ftxui::ScreenInteractive::Fullscreen()),
          input_(ftxui::Input(&input_text_, "Search...")),
          items_component_(ftxui::Menu(&submitted_items_, &selected_index_)) {
        setup_components();
    }

    void run() {
        screen_.Loop(root_);
    }

    void add_item(const SearchMetaData& item) {
        submitted_items_.push_back(item.title);
        submitted_items_data_.push_back(item);
    }

    void clear_items() {
        submitted_items_.clear();
        selected_index_ = 0;
    }

    void set_audio_position(double current_seconds) {
        current_time_seconds_ = std::max(0.0, current_seconds);
    }

    void set_audio_duration(double total_seconds) {
        total_time_seconds_ = std::max(0.0, total_seconds);
    }

    void set_audio_times(double current_seconds, double total_seconds) {
        current_time_seconds_ = std::max(0.0, current_seconds);
        total_time_seconds_ = std::max(0.0, total_seconds);
    }

private:
    enum class ActivePanel {
        Input,
        Items
    };

    void setup_components() {
        input_with_handler_ = ftxui::CatchEvent(input_, [&](ftxui::Event event) {
            if (active_panel_ != ActivePanel::Input) {
                return false;
            }

            if (event == ftxui::Event::Return) {
                on_submit();
                return true;
            }

            return false;
        });

        items_with_handler_ = ftxui::CatchEvent(items_component_, [&](ftxui::Event event) {
            if (active_panel_ != ActivePanel::Items) {
                return true;
            }

            if (event == ftxui::Event::Return) {
                on_item_action();
                return true;
            }

            return false;
        });

        container_ = ftxui::Container::Vertical({
            input_with_handler_,
            items_with_handler_,
        });

        root_ = ftxui::CatchEvent(container_, [&](ftxui::Event event) {
            if (event == ftxui::Event::Tab) {
                switch_panel();
                return true;
            }
            return false;
        });

        root_ = ftxui::Renderer(root_, [&] {
            auto input_box =
                ftxui::window(
                    ftxui::text(active_panel_ == ActivePanel::Input ? " Input * " : " Input "),
                    ftxui::vbox({
                        input_with_handler_->Render() | ftxui::xflex,
                    })
                ) | ftxui::xflex;

            ftxui::Element items_content;
            if (submitted_items_.empty()) {
                items_content = ftxui::text("No items yet.");
            } else {
                items_content = items_with_handler_->Render();
            }

            auto items_box =
                ftxui::window(
                    ftxui::text(active_panel_ == ActivePanel::Items ? " Items * " : " Items "),
                    ftxui::vbox({
                        items_content | ftxui::yflex,
                    })
                ) | ftxui::xflex | ftxui::yflex;

            auto help_box =
                ftxui::window(
                    ftxui::text(" Help "),
                    ftxui::vbox({
                        ftxui::text("Tab: switch panels"),
                        ftxui::text("Input panel: Enter submits"),
                        ftxui::text("Items panel: Up/Down select, Enter acts"),
                    })
                ) | ftxui::xflex;

            auto status_bar =
                ftxui::window(
                    ftxui::text(" Audio "),
                    ftxui::hbox({
                        ftxui::text(format_time(current_time_seconds_)),
                        ftxui::text(" "),
                        ftxui::gauge(compute_progress(current_time_seconds_, total_time_seconds_)) | ftxui::xflex,
                        ftxui::text(" "),
                        ftxui::text(format_time(total_time_seconds_)),
                    })
                ) | ftxui::xflex;

            return ftxui::vbox({
                       input_box,
                       items_box,
                       help_box,
                       status_bar,
                   }) |
                   ftxui::xflex | ftxui::yflex;
        });
    }

    void switch_panel() {
        if (active_panel_ == ActivePanel::Input) {
            active_panel_ = ActivePanel::Items;
        } else {
            active_panel_ = ActivePanel::Input;
        }
    }

    void on_submit() {
        if (input_text_.empty()) {
            return;
        }

        clear_items();

        std::vector<SearchMetaData> results = search::get_search_results(input_text_);

        for (const auto& item : results) {
            add_item(item);
        }

        input_text_.clear();

        if (selected_index_ >= static_cast<int>(submitted_items_.size())) {
            selected_index_ = static_cast<int>(submitted_items_.size()) - 1;
        }
        if (selected_index_ < 0) {
            selected_index_ = 0;
        }
    }

    void on_item_action() {
        if (submitted_items_.empty()) {
            return;
        }

        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(submitted_items_.size())) {
            return;
        }

        std::string selected_url = submitted_items_data_[selected_index_].url;
        std::string stream_url = search::get_stream_url(selected_url);

        std::thread([stream_url]() {
            try {
                audio::AudioStreamPlayer player;
                player.play(stream_url);
            } catch (const std::exception& ex) {
                std::cerr << "Audio error: " << ex.what() << '\n';
            }
        }).detach();

        std::cout << "Selected item action: " << selected_url << std::endl;
    }

private:
    ftxui::ScreenInteractive screen_;

    std::string input_text_;
    std::vector<std::string> submitted_items_;
    std::vector<SearchMetaData> submitted_items_data_;
    int selected_index_ = 0;

    double current_time_seconds_ = 83.0;
    double total_time_seconds_ = 225.0;

    ActivePanel active_panel_ = ActivePanel::Input;

    ftxui::Component input_;
    ftxui::Component items_component_;

    ftxui::Component input_with_handler_;
    ftxui::Component items_with_handler_;
    ftxui::Component container_;
    ftxui::Component root_;
};

int main() {
    TUI tui;
    tui.run();
    return 0;
}
