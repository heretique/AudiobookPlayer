#include "AudiobookPlayer.h"
#include "vlc/vlc.h"
#include "enkiTS/TaskScheduler.h"
#include "sqlite3pp/sqlite3pp.h"
#include "imgui.h"
#include "imFileBroser.h"
#include "imSpinner.h"
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


static const std::string kLibraryDb = "library.db";
static const std::string kInitialized = "Initialized...";
static const std::string kChooseLibraryLocation = "Choose Library Location";
static const std::string kCreateBooksTable = "create table if not exists books (key integer unique primary key, author text, name text, series text, duration integer)";

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
    sqlite3pp::database _libraryDb;
    std::unique_ptr<enki::TaskScheduler> _taskScheduler;
    std::unique_ptr<enki::TaskSet> _currentTask;


    Library()
        : _taskScheduler(std::make_unique<enki::TaskScheduler>())
    {}

    ~Library() {
        _taskScheduler->WaitforAllAndShutdown();
        _libraryDb.disconnect();
    }

    bool isEmpty() { return true; }
    bool init() {
        _taskScheduler->Initialize();

        // Initialize database
        _libraryDb.disconnect();
        fs::path dbPath = fs::current_path();
        dbPath.append(kLibraryDb);
        int result = _libraryDb.connect(dbPath.string().c_str(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (SQLITE_OK != result )
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
        _currentTask = std::make_unique<enki::TaskSet>([pathName, this](enki::TaskSetPartition range, uint32_t threadnum) {
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

}; // struct Library

struct AudiobookPlayerImpl
{
    libvlc_instance_t*                   _vlcInstance {nullptr};
    libvlc_media_player_t*               _mediaPlayer {nullptr};
    libvlc_media_t*                      _currentMedia {nullptr};
    PlayerState                          _state {PlayerState::Initialized};
    Library _library;
    std::string _status;

    AudiobookPlayerImpl::AudiobookPlayerImpl()
    {
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

    void changeState(PlayerState state)
    {
        switch (_state)
        {
            case PlayerState::Initialized:
                onExitInitialized();
                break;
            case PlayerState::Empty:
                onExitEmpty();
                break;
            case PlayerState::LibraryDiscovery:
                onExitLibraryDiscovery();
                break;
            case PlayerState::LibraryParsing:
                onEnterLibraryParsing();
                break;
            case PlayerState::Settings:
                onExitSettings();
                break;
            case PlayerState::Library:
                onExitLibrary();
                break;
            case PlayerState::BookInfo:
                onExitBookInfo();
                break;
            case PlayerState::Player:
                onExitPlayer();
                break;
            default:
                assert(false && "this should not be reached, make sure you handle all cases!");
                break;
        }

        _state = state;

        switch (_state)
        {
            case PlayerState::Initialized:
                onEnterInitialized();
                break;
            case PlayerState::Empty:
                onEnterEmpty();
                break;
            case PlayerState::LibraryDiscovery:
                onEnterLibraryDiscovery();
                break;
            case PlayerState::LibraryParsing:
                onEnterLibraryParsing();
                break;
            case PlayerState::Settings:
                onEnterSettings();
                break;
            case PlayerState::Library:
                onEnterLibrary();
                break;
            case PlayerState::BookInfo:
                onEnterBookInfo();
                break;
            case PlayerState::Player:
                onEnterPlayer();
                break;
            default:
                assert(false && "this should not be reached, make sure you handle all cases!");
                break;
        }
    }

    void update()
    {
        switch (_state)
        {
            case PlayerState::Initialized:
                onUpdateInitialized();
                break;
            case PlayerState::Empty:
                onUpdateEmpty();
                break;
            case PlayerState::LibraryDiscovery:
                onUpdateLibraryDiscovery();
                break;
            case PlayerState::LibraryParsing:
                onUpdateLibraryParsing();
                break;
            case PlayerState::Settings:
                onUpdateSettings();
                break;
            case PlayerState::Library:
                onUpdateLibrary();
                break;
            case PlayerState::BookInfo:
                onUpdateBookInfo();
                break;
            case PlayerState::Player:
                onUpdatePlayer();
                break;
            default:
                assert(false && "this should not be reached, make sure you handle all cases!");
                break;
        }

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
    void onEnterInitialized() { }
    void onUpdateInitialized() {
        if (_library.isEmpty())
        {
            changeState(PlayerState::Empty);
            return;
        }
        else
        {

        }
    }
    void onExitInitialized() { }

    // PlayerState::Empty
    void onEnterEmpty() { }
    void onUpdateEmpty()
    {
        static bool showDialog = false;
        static std::string location;

        if (ImGui::Button("Choose library location"))
        {
            showDialog = true;
        }

        if (showDialog && ImGui::FileBrowser(kChooseLibraryLocation, location, showDialog, ImGuiFileBrowserFlags_SelectDirectory))
        {
            if (_library.startLibraryDiscovery(location))
            {
                changeState(PlayerState::LibraryDiscovery);
            }
        }
    }
    void onExitEmpty() { }

    // PlayerState::LibraryDiscovery
    void onEnterLibraryDiscovery() {
        _status = "Searching books...";
    }
    void onUpdateLibraryDiscovery() {
        ImGui::SetCursorPos((ImGui::GetWindowSize() - 200.f) / 2.0f);
        ImGui::SpinnerCircle("Library Discovery...", 100.f, 
            ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_ButtonHovered)), 
            ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_FrameBg)), 16, 2.0f);
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

