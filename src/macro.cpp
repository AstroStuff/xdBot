#include "includes.hpp"
#include "ui/record_layer.hpp"
#include "ui/game_ui.hpp"

#include <Geode/modify/PlayLayer.hpp>

void Macro::recordAction(int frame, int button, bool player2, bool hold) {
    auto& g = Global::get();

    if (g.macro.inputs.empty())
        Macro::updateInfo(PlayLayer::get());

    g.macro.inputs.push_back(input(frame, button, player2, hold));
}

void Macro::tryAutosave(GJGameLevel* level, CheckpointObject* cp) {
    auto& g = Global::get();

    if (g.state != state::recording) return;
    if (!Mod::get()->getSavedValue<bool>("macro_auto_save")) return;
    if (!g.checkpoints.contains(cp)) return;
    if (g.checkpoints[cp].frame < g.lastAutoSave) return;

    std::filesystem::path autoSavesPath = g.mod->getSaveDir() / "autosaves";

    if (!std::filesystem::exists(autoSavesPath)) {

        if (!std::filesystem::create_directory(autoSavesPath))
            return log::debug("Failed to create auto saves path.");

    }

    std::string levelname = level->m_levelName;
    std::filesystem::path path = autoSavesPath / fmt::format("autosave_{}_{}", levelname, g.currentSession);
    std::filesystem::remove(path.string() + ".gdr"); // Remove previous save

    GJAccountManager* accManager = GJAccountManager::sharedState();

    if (!accManager)
        return log::debug("Failed to autosave macro. ID: 2");

    std::string username = accManager->m_username;
    int result = Macro::save(username, "", path.string());

    if (result != 0)
        log::debug("Failed to autosave macro. ID: {}. Path: {}.gdr", result, path);

}

void Macro::updateInfo(PlayLayer* pl) {
    if (!pl) return;

    auto& g = Global::get();

    if (g.macro.canChangeFPS) {
        g.macro.canChangeFPS = false;
        g.macro.framerate = 240.f;
    }

    GJGameLevel* level = pl->m_level;

    if (level->m_lowDetailModeToggled != g.macro.ldm)
        g.macro.ldm = level->m_lowDetailModeToggled;

    int id = level->m_levelID.value();
    if (id != g.macro.levelInfo.id)
        g.macro.levelInfo.id = id;

    std::string name = level->m_levelName;
    if (name != g.macro.levelInfo.name)
        g.macro.levelInfo.name = name;

    std::string author = GJAccountManager::sharedState()->m_username;
    if (g.macro.author != author)
        g.macro.author = author;

    if (g.macro.author == "")
        g.macro.author = "N/A";

    g.macro.botInfo.name = "xdBot";
    g.macro.botInfo.version = xdBotVersion;
}

int Macro::save(std::string author, std::string desc, std::string path, bool json) {
    auto& g = Global::get();

    if (g.macro.inputs.empty()) return 30;

    std::string extension = json ? ".gdr.json" : ".gdr";

    int iterations = 0;

    while (std::filesystem::exists(path + extension)) {
        iterations++;

        if (iterations > 1) {
            int length = 3 + std::to_string(iterations - 1).length();
            path.erase(path.length() - length, length);
        }

        path += fmt::format(" ({})", std::to_string(iterations));
    }

    path += extension;

    log::debug("Saving macro to path: {}", path);

    g.macro.author = author;
    g.macro.description = desc;
    g.macro.duration = g.macro.inputs.back().frame / g.macro.framerate;

    std::wstring widePath = Utils::widen(path);

    if (widePath == L"Widen Error")
        return 30;

    std::ofstream f(widePath, std::ios::binary);

    if (!f)
        f.open(path, std::ios::binary);

    if (!f)
        return 20;

    std::vector<gdr::FrameFix> frameFixes = g.macro.frameFixes;

    auto data = g.macro.exportData(json);

    f.write(reinterpret_cast<const char*>(data.data()), data.size());

    if (!f) {
        f.close();
        return 21;
    }

    if (!f)
        return 22;

    f.close();

    return 0;
}

bool Macro::loadXDFile(std::filesystem::path path) {

    Macro newMacro = Macro::XDtoGDR(path);
    if (newMacro.description == "fail")
        return false;

    Global::get().macro = newMacro;
    return true;
}

Macro Macro::XDtoGDR(std::filesystem::path path) {

    Macro newMacro;
    newMacro.author = "N/A";
    newMacro.description = "N/A";
    newMacro.gameVersion = GEODE_GD_VERSION;

    std::ifstream file(Utils::widen(path.string()));
    std::string line;

    if (!file.is_open()) {
        newMacro.description = "fail";
        return newMacro;
    }

    bool firstIt = true;
    bool andr = false;

    float fpsMultiplier = 1.f;

    while (std::getline(file, line)) {
        std::string item;
        std::stringstream ss(line);
        std::vector<std::string> action;

        while (std::getline(ss, item, '|'))
            action.push_back(item);

        if (action.size() < 4) {
            if (action[0] == "android")
                fpsMultiplier = 4.f;
            else {
                int fps = std::stoi(action[0]);
                fpsMultiplier = 240.f / fps;
            }

            continue;
        }

        int frame = static_cast<int>(round(std::stoi(action[0]) * fpsMultiplier));
        int button = std::stoi(action[2]);
        bool hold = action[1] == "1";
        bool player2 = action[3] == "1";
        bool posOnly = action[4] == "1";

        if (!posOnly)
            newMacro.inputs.push_back(input(frame, button, player2, hold));
        else {
            cocos2d::CCPoint p1Pos = ccp(std::stof(action[5]), std::stof(action[6]));
            cocos2d::CCPoint p2Pos = ccp(std::stof(action[11]), std::stof(action[12]));

            newMacro.frameFixes.push_back({ frame, {p1Pos, 0.f, false}, {p2Pos, 0.f, false} });
        }
    }

    file.close();

    return newMacro;

}

void Macro::resetVariables() {
    auto& g = Global::get();

    g.ignoreFrame = -1;
    g.ignoreJumpButton = -1;

    g.delayedFrameReleaseMain[0] = -1;
    g.delayedFrameReleaseMain[1] = -1;

    g.delayedFrameInput[0] = -1;
    g.delayedFrameInput[1] = -1;

    g.addSideHoldingMembers[0] = false;
    g.addSideHoldingMembers[1] = false;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++)
            g.delayedFrameRelease[x][y] = -1;
    }
}

void Macro::resetState(bool cp) {
    auto& g = Global::get();

    g.restart = false;
    g.state = state::none;

    if (!cp)
        g.checkpoints.clear();

    Interface::updateLabels();
    Interface::updateButtons();

    Macro::resetVariables();
}

void Macro::togglePlaying() {
    if (Global::hasIncompatibleMods()) return;

    auto& g = Global::get();
    
    if (g.layer) {
        static_cast<RecordLayer*>(g.layer)->playing->toggle(Global::get().state != state::playing);
        static_cast<RecordLayer*>(g.layer)->togglePlaying(nullptr);
        return;
    }

    RecordLayer* layer = RecordLayer::create();
    layer->togglePlaying(nullptr);
    layer->onClose(nullptr);
}

void Macro::toggleRecording() {
    if (Global::hasIncompatibleMods()) return;
    
    auto& g = Global::get();
    
    if (g.layer) {
      static_cast<RecordLayer*>(g.layer)->recording->toggle(Global::get().state != state::recording);
      static_cast<RecordLayer*>(g.layer)->toggleRecording(nullptr);
      return;
    }

    RecordLayer* layer = RecordLayer::create();
    layer->toggleRecording(nullptr);
    layer->onClose(nullptr);
}

bool Macro::shouldStep() {
    auto& g = Global::get();

    if (g.stepFrame) return true;
    if (Global::getCurrentFrame() == 0) return true;

    // if (g.ignoreFrame != -1) return true;
    // if (g.ignoreJumpButton != -1) return true;

    // if (g.delayedFrameReleaseMain[0] != -1) return true;
    // if (g.delayedFrameReleaseMain[1] != -1) return true;

    // if (g.delayedFrameInput[0] != -1) return true;
    // if (g.delayedFrameInput[1] != -1) return true;

    // for (int x = 0; x < 2; x++) {
    //     for (int y = 0; y < 2; y++) {
    //         if (g.delayedFrameRelease[x][y] != -1) return true;
    //     }
    // }

    return false;
}