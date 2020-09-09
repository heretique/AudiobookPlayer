#include "AudiobookPlayer.h"
#include "vlc/vlc.h"
#include "enkiTS/TaskScheduler.h"
#include "sqlite3pp/sqlite3pp.h"
#include "imgui.h"
#include "imFileBroser.h"
#include "imSpinner.h"
#include "StateMachine.h"
#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

enum class PlayerState
{
    Initialized,
    Empty,
    LibraryDiscovery,
    LibraryParsing,
    Settings,
    Library,
    BookInfo,
    Player,
};

static const std::string kLibraryDb             = "library.db";
static const std::string kInitialized           = "Initialized...";
static const std::string kChooseLibraryLocation = "Choose Library Location";
static const std::string kCreateBooksTable =
    "create table if not exists books (key integer unique primary key, author text, name text, series text, duration integer)";

struct BookInfo
{
};

struct TrackInfo
{
};

struct MetaInfo
{
};

ImVec2 operator/(const ImVec2& lhs, float s)
{
    return ImVec2(lhs.x / s, lhs.y / s);
}

ImVec2 operator-(const ImVec2& lhs, float s)
{
    return ImVec2(lhs.x - s, lhs.y - s);
}

struct Library
{
    sqlite3pp::database                  _libraryDb;
    std::unique_ptr<enki::TaskScheduler> _taskScheduler;
    std::unique_ptr<enki::TaskSet>       _currentTask;

    Library()
        : _taskScheduler(std::make_unique<enki::TaskScheduler>())
    {
    }

    ~Library()
    {
        _taskScheduler->WaitforAllAndShutdown();
        _libraryDb.disconnect();
    }

    bool isEmpty()
    {
        return true;
    }
    bool init()
    {
        _taskScheduler->Initialize();

        // Initialize database
        _libraryDb.disconnect();
        fs::path dbPath = fs::current_path();
        dbPath.append(kLibraryDb);
        int result = _libraryDb.connect(dbPath.string().c_str(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (SQLITE_OK != result)
        {
            return false;
        }

        result = _libraryDb.execute(kCreateBooksTable.c_str());
        if (SQLITE_OK != result)
        {
            return false;
        }

        return true;
    }

    bool startLibraryDiscovery(const std::string& pathName)
    {
        _currentTask =
            std::make_unique<enki::TaskSet>([pathName, this](enki::TaskSetPartition range, uint32_t threadnum) {
                fs::path path(pathName);
                if (!fs::exists(path) || !fs::is_directory(path))
                {
                    return;
                }
                fs::recursive_directory_iterator rdi(path);

                for (const auto& entry : rdi)
                {
                    std::cout << entry.path() << '\n';
                }
            });
        _taskScheduler->AddTaskSetToPipe(_currentTask.get());

        return true;
    }

};  // struct Library

struct AudiobookPlayerImpl
{
    using SM = StateMachine<PlayerState>;

    libvlc_instance_t*     _vlcInstance {nullptr};
    libvlc_media_player_t* _mediaPlayer {nullptr};
    libvlc_media_t*        _currentMedia {nullptr};
    SM                     _stateMachine;
    Library                _library;
    std::string            _status;

    AudiobookPlayerImpl::AudiobookPlayerImpl()
        : _stateMachine(
              PlayerState::Initialized,
              []() { return SM::ResultType(); },
              [this]() { return this->onUpdateInitialized(); },
              []() { return SM::ResultType(); })
    {
        _stateMachine.addState(
            PlayerState::Empty,
            [this]() { return this->onEnterEmpty(); },
            [this]() { return this->onUpdateEmpty(); },
            []() {});
        _stateMachine.addState(
            PlayerState::LibraryDiscovery,
            [this]() { return this->onEnterLibraryDiscovery(); },
            [this]() { return this->onUpdateLibraryDiscovery(); },
            []() {});
    }

    AudiobookPlayerImpl::~AudiobookPlayerImpl()
    {
        if (_vlcInstance)
        {
            libvlc_release(_vlcInstance);
        }
    }

    bool init(int argc, const char* const* argv)
    {
        _vlcInstance = libvlc_new(argc, argv);
        if (_vlcInstance == nullptr)
        {
            return false;
        }
        if (!_library.init())
        {
            return false;
        }

        _status = kInitialized;
        return true;
    }

    bool openMedia(const std::string& path)
    {
        assert(_vlcInstance);

        _currentMedia = libvlc_media_new_path(_vlcInstance, path.c_str());
        if (!_currentMedia)
        {
            return false;
        }

        _mediaPlayer = libvlc_media_player_new_from_media(_currentMedia);
        libvlc_media_release(_currentMedia);

        libvlc_media_player_play(_mediaPlayer);

        return true;
    }

    void update()
    {
        _stateMachine.tick();
        drawStatus();
    }

    void drawStatus()
    {
        ImGui::SetCursorPosX(0);
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 2 * ImGui::GetFontSize());
        ImGui::Separator();
        ImGui::Text(_status.c_str());
    }

    // PlayerState::Initialized
    SM::ResultType onUpdateInitialized()
    {
        if (_library.isEmpty())
        {
            return PlayerState::Empty;
        }
        else
        {
        }
        return {};
    }

    // PlayerState::Empty
    SM::ResultType onEnterEmpty()
    {
        return {};
    }
    SM::ResultType onUpdateEmpty()
    {
        static bool        showDialog = false;
        static std::string location;

        if (ImGui::Button("Choose library location"))
        {
            showDialog = true;
        }

        if (showDialog &&
            ImGui::FileBrowser(kChooseLibraryLocation, location, showDialog, ImGuiFileBrowserFlags_SelectDirectory))
        {
            if (_library.startLibraryDiscovery(location))
            {
                return PlayerState::LibraryDiscovery;
            }
        }

        return {};
    }

    // PlayerState::LibraryDiscovery
    SM::ResultType onEnterLibraryDiscovery()
    {
        _status = "Searching books...";
        return {};
    }
    SM::ResultType onUpdateLibraryDiscovery()
    {
        ImGui::SetCursorPos((ImGui::GetWindowSize() - 200.f) / 2.0f);
        ImGui::SpinnerCircle("Library Discovery...", 100.f,
                             ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_ButtonHovered)),
                             ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_FrameBg)), 16, 2.0f);

        return {};
    }
    void onExitLibraryDiscovery() { }

    // PlayerState::LibraryParsing
    void onEnterLibraryParsing() { }
    void onUpdateLibraryParsing() { }
    void onExitLibraryParsing() { }

    // PlayerState::Settings
    void onEnterSettings() { }
    void onUpdateSettings() { }
    void onExitSettings() { }

    // PlayerState::Library
    void onEnterLibrary() { }
    void onUpdateLibrary() { }
    void onExitLibrary() { }

    // PlayerState::BookInfo
    void onEnterBookInfo() { }
    void onUpdateBookInfo() { }
    void onExitBookInfo() { }

    // PlayerState::Player
    void onEnterPlayer() { }
    void onUpdatePlayer() { }
    void onExitPlayer() { }
};

AudiobookPlayer::AudiobookPlayer()
    : _impl(std::make_unique<AudiobookPlayerImpl>())
{
}

AudiobookPlayer::~AudiobookPlayer() { }

void AudiobookPlayer::update()
{
    _impl->update();
}

bool AudiobookPlayer::init(int argc, const char* const* argv)
{
    return _impl->init(argc, argv);
}
