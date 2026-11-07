#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <eclipse.eclipse-menu/include/eclipse.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <vector>

using namespace geode::prelude;

namespace {
    // M held / "M was used together with a number" flags
    bool s_mDown = false;
    bool s_comboUsed = false;

    void notify(std::string const& text, NotificationIcon icon) {
        Notification::create(text, icon, 0.6f)->show();
    }

    // Replays folder of Eclipse Menu
    std::filesystem::path replaysDir() {
        auto eclipse = Loader::get()->getLoadedMod("eclipse.eclipse-menu");
        if (!eclipse) return {};
        return eclipse->getSaveDir() / "replays";
    }

    // All replays sorted by file name: 1st = M+1, 2nd = M+2, ...
    std::vector<std::filesystem::path> listReplays() {
        std::vector<std::filesystem::path> out;
        auto dir = replaysDir();
        std::error_code ec;
        if (dir.empty() || !std::filesystem::exists(dir, ec)) return out;

        for (auto const& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
            std::error_code ec2;
            if (!entry.is_regular_file(ec2)) continue;
            auto ext = entry.path().extension().string();
            if (ext == ".gdr2" || ext == ".gdr") out.push_back(entry.path());
        }

        auto lower = [](std::filesystem::path const& p) {
            auto n = p.filename().string();
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return n;
        };
        std::sort(out.begin(), out.end(), [&](auto const& a, auto const& b) { return lower(a) < lower(b); });
        return out;
    }

    // M: toggle playback of the currently selected replay (loads it first)
    void toggleReplay() {
        int state = eclipse::config::get<int>("bot.state", 0);

        if (state == 2) {
            eclipse::config::set<int>("bot.state", 0);
            notify("Replay: OFF", NotificationIcon::Info);
            return;
        }

        // loading would wipe the inputs that are being recorded
        if (state == 1) {
            notify("Stop recording first", NotificationIcon::Warning);
            return;
        }

        std::filesystem::path path = eclipse::config::get<std::string>("bot.selectedreplay", "");
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec)) {
            notify("No replay selected", NotificationIcon::Warning);
            return;
        }

        auto res = eclipse::loadReplay(path);
        if (res.isErr()) {
            notify("Failed to load replay", NotificationIcon::Error);
            return;
        }

        eclipse::config::set<int>("bot.state", 2);
        notify("Replay: ON", NotificationIcon::Success);
    }

    // M + number: select the Nth replay and load it (no "Load" button needed)
    void selectReplay(size_t index) {
        if (eclipse::config::get<int>("bot.state", 0) == 1) {
            notify("Stop recording first", NotificationIcon::Warning);
            return;
        }

        auto replays = listReplays();
        if (index == 0 || index > replays.size()) {
            notify(fmt::format("No replay #{}", index), NotificationIcon::Warning);
            return;
        }

        auto const& path = replays[index - 1];
        auto res = eclipse::loadReplay(path);
        if (res.isErr()) {
            notify(fmt::format("Failed to load #{}", index), NotificationIcon::Error);
            return;
        }

        eclipse::config::set<std::string>("bot.selectedreplay", utils::string::pathToString(path));
        notify(
            fmt::format("Replay #{}: {}", index, utils::string::pathToString(path.filename().stem())),
            NotificationIcon::Success
        );
    }

    // 1..9 (top row or numpad) -> 1..9, anything else -> 0
    int digitOf(enumKeyCodes key) {
        switch (key) {
            case KEY_One: case KEY_NumPad1: return 1;
            case KEY_Two: case KEY_NumPad2: return 2;
            case KEY_Three: case KEY_NumPad3: return 3;
            case KEY_Four: case KEY_NumPad4: return 4;
            case KEY_Five: case KEY_NumPad5: return 5;
            case KEY_Six: case KEY_NumPad6: return 6;
            case KEY_Seven: case KEY_NumPad7: return 7;
            case KEY_Eight: case KEY_NumPad8: return 8;
            case KEY_Nine: case KEY_NumPad9: return 9;
            default: return 0;
        }
    }
}

$execute {
    KeyboardInputEvent().listen([](KeyboardInputData& data) {
        // don't react while typing in a text field
        if (CCIMEDispatcher::sharedDispatcher()->hasDelegate()) {
            return ListenerResult::Propagate;
        }

        if (data.action == KeyboardInputData::Action::Repeat) {
            return ListenerResult::Propagate;
        }
        bool down = data.action == KeyboardInputData::Action::Press;

        if (data.key == KEY_M) {
            if (down) {
                s_mDown = true;
                s_comboUsed = false;
            } else if (s_mDown) {
                s_mDown = false;
                // M alone (released without a number) toggles the replay
                if (!s_comboUsed) toggleReplay();
            }
            return ListenerResult::Propagate;
        }

        if (down && s_mDown) {
            if (int digit = digitOf(data.key)) {
                s_comboUsed = true;
                selectReplay(static_cast<size_t>(digit));
                return ListenerResult::Stop;
            }
        }

        return ListenerResult::Propagate;
    }).leak();
}
