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
#include <unordered_set>

namespace fs = std::filesystem;

// ENUMS

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

enum class TrackType
{
    Unknown,
    Audio,
    Video,
    Text
};

// LITERALS

// Library db
static const std::string kLibraryDb             = "library.db";
static const std::string kInitialized           = "Initialized...";
static const std::string kChooseLibraryLocation = "Choose Library Location";
static const std::string kCreateBooksTable =
    "create table if not exists books (key integer unique primary key, duration integer, author text, name text, series text, description text, path text, thumbnail_path)";
static const std::string kCreateFilesTable =
    "create table if not exists files (key integer unique primary key, book_id integer, last_modified integer, track_number integer, path text)";
static const std::string kCreateBookmarksTable =
    "create table if not exists bookmarks (key integer unique primary key, book_id integer, name text, file_id, position integer, description text)";
static const std::string kCreateSettingsTable =
    "create table if not exists settings (setting text unique primary key, value text)";
static const std::string kLastBookMarkName = "##last##";

// settings
static const std::string kSettingLastBookId = "last_book_id";
static const std::string kPlayingSpeed      = "playing_speed";

// Extensions
static const std::unordered_set<std::string> kIgnoreExtensions = {
    ".nfo", ".txt", ".pdf", ".epub", ".mobi", ".log", ".png", ".jpg", ".jpeg", ".gif", ".ico", ".bmp", ".tga"};
static const std::unordered_set<std::string> kPlaylistExtensions = {".m3u"};

// UTILITIES

template <typename E>
constexpr auto toUnderlyingType(E e)
{
    return static_cast<typename std::underlying_type<E>::type>(e);
}

ImVec2 operator/(const ImVec2& lhs, float s)
{
    return ImVec2(lhs.x / s, lhs.y / s);
}

ImVec2 operator-(const ImVec2& lhs, float s)
{
    return ImVec2(lhs.x - s, lhs.y - s);
}

const char* ValueOrEmpty(const char* s)
{
    return s == nullptr ? "" : s;
}

TrackType FromVLCTrackType(libvlc_track_type_t type)
{
    TrackType trackType = TrackType::Unknown;
    switch (type)
    {
        case libvlc_track_audio:
            trackType = TrackType::Audio;
            break;
        case libvlc_track_video:
            trackType = TrackType::Video;
            break;
        case libvlc_track_text:
            trackType = TrackType::Text;
            break;
    }
    return trackType;
}

// STRUCTS & CLASSES

struct Track
{
    TrackType type;
};

struct Meta
{
    std::string author;
    std::string name;
    std::string rating;
    std::string artworkUrl;
    std::string publisher;
    std::string trackNumber;
    std::string description;
};

struct Media
{
    uint32_t           id {0};
    std::string        path;
    int64_t            duration {0};
    int64_t            lastModified;
    uint32_t           trackNumber;
    Meta               meta;
    std::vector<Track> tracks;
    bool               isPlaylist {false};

    bool isEmpty() const
    {
        return duration == 0 && tracks.size() == 0;
    }
};

struct Bookmark
{
};

struct Book
{
    uint32_t           id {0};
    std::string        folder;
    std::string        author;
    std::string        name;
    std::string        series;
    std::string        description;
    uint64_t           duration {0};
    std::string        thumbnailLocation;
    std::vector<Media> files;
};

struct Library
{
    sqlite3pp::database                  _libraryDb;
    std::unique_ptr<enki::TaskScheduler> _taskScheduler;
    std::unique_ptr<enki::TaskSet>       _currentTask;
    libvlc_instance_t*                   _vlcInstance {nullptr};  // shared VLC instance with the player
    std::vector<Book>                    _books;
    std::unique_ptr<Book>                _currentBook;

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
        return _books.empty();
    }

    bool init(libvlc_instance_t* vlcInstance)
    {
        _vlcInstance = vlcInstance;
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
        result = _libraryDb.execute(kCreateFilesTable.c_str());
        if (SQLITE_OK != result)
        {
            return false;
        }
        result = _libraryDb.execute(kCreateBookmarksTable.c_str());
        if (SQLITE_OK != result)
        {
            return false;
        }
        result = _libraryDb.execute(kCreateSettingsTable.c_str());
        if (SQLITE_OK != result)
        {
            return false;
        }

        return true;
    }

    bool startLibraryDiscovery(const std::string& pathName)
    {
        _currentTask = std::make_unique<enki::TaskSet>([pathName, this](enki::TaskSetPartition range,
                                                                        uint32_t               threadnum) {
            fs::path path(pathName);
            if (!fs::exists(path) || !fs::is_directory(path))
            {
                return;
            }

            _books.clear();
            fs::recursive_directory_iterator rdi(path);
            bool                             wasPreviousDirectory = false;
            for (const auto& entry : rdi)
            {
                std::cout << entry.path() << '\n';
                if (entry.is_directory())
                {
                    if (wasPreviousDirectory && !_books.empty())
                    {
                        _books.pop_back();
                    }
                    wasPreviousDirectory = true;
                    Book newBook;
                    newBook.name   = entry.path().filename().string();
                    newBook.folder = entry.path().string();
                    _books.emplace_back(newBook);
                }
                else if (entry.is_regular_file())
                {
                    wasPreviousDirectory = false;
                    if (kIgnoreExtensions.find(entry.path().extension().string()) != kIgnoreExtensions.end())
                    {
                        // skip this file
                        continue;
                    }

                    libvlc_media_t* media = libvlc_media_new_path(_vlcInstance, entry.path().string().c_str());
                    if (!media)
                    {
                        // couldn't load media, skip
                        continue;
                    }

                    libvlc_media_parse(media);

                    if (!_books.empty())
                    {
                        Book& lastBook = _books.back();
                        Media mediaInfo;
                        mediaInfo.path = entry.path().string();
                        if (kPlaylistExtensions.find(entry.path().extension().string()) != kPlaylistExtensions.end())
                        {
                            mediaInfo.isPlaylist = true;
                        }
                        auto lastModifiedTime = fs::last_write_time(entry.path());

                        mediaInfo.lastModified =
                            std::chrono::duration_cast<std::chrono::milliseconds>(lastModifiedTime.time_since_epoch())
                                .count();
                        readMediaInfo(media, mediaInfo);
                        readMediaMeta(media, mediaInfo.meta);
                        lastBook.files.emplace_back(mediaInfo);
                    }

                    libvlc_media_release(media);
                }
            }
            std::cout << "Found books:\n";
            for (const auto& book : _books)
            {
                writeBookToDb(book);
                std::cout << book.name << "\n";
                for (const auto& file : book.files)
                {
                    std::cout << "\t"
                              << "File: " << file.path << "\n";
                    std::cout << "\t"
                              << "Author: " << file.meta.author << "\n";
                    std::cout << "\t"
                              << "Name: " << file.meta.name << "\n";
                    std::cout << "\t"
                              << "Is playlist: " << file.isPlaylist << "\n";
                    std::cout << "\t"
                              << "Description: " << file.meta.description << "\n";
                    std::cout << "\t"
                              << "Track number: " << file.meta.trackNumber << "\n";
                    std::cout << "\t"
                              << "Duration: " << file.duration << "\n";
                    for (const auto& track : file.tracks)
                    {
                        std::cout << "\t"
                                  << "Track type: " << toUnderlyingType(track.type) << "\n";
                    }
                }
            }
        });
        _taskScheduler->AddTaskSetToPipe(_currentTask.get());

        return true;
    }

    void readMediaInfo(libvlc_media_t* const media, Media& outInfo) const
    {
        libvlc_media_track_t** tracksInfo;
        outInfo.duration = libvlc_media_get_duration(media);

        auto numTracks = libvlc_media_tracks_get(media, &tracksInfo);
        for (auto i = 0u; i < numTracks; ++i)
        {
            Track trackInfo;
            readTrackInfo(tracksInfo[i], trackInfo);
            outInfo.tracks.emplace_back(trackInfo);
        }
        libvlc_media_tracks_release(tracksInfo, numTracks);
    }

    void readMediaMeta(libvlc_media_t* const media, Meta& outMeta) const
    {
        assert(media);
        outMeta.author      = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_Artist));
        outMeta.name        = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_Title));
        outMeta.rating      = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_Rating));
        outMeta.artworkUrl  = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_ArtworkURL));
        outMeta.publisher   = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_Publisher));
        outMeta.trackNumber = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_TrackNumber));
        outMeta.description = ValueOrEmpty(libvlc_media_get_meta(media, libvlc_meta_Description));
    }

    void readTrackInfo(libvlc_media_track_t* track, Track& trackInfo) const
    {
        assert(track);
        trackInfo.type = FromVLCTrackType(track->i_type);
    }

    void clearDb() { }

    void setDefaultSettings()
    {
        // removeFrom database
        sqlite3pp::command cmd(_libraryDb, "drop table settings");
        if (SQLITE_OK != cmd.execute())
        {
            std::cout << "Failed to drop settings table" << std::endl;
        }
    }

    bool writeBookToDb(const Book& bookInfo)
    {
        // start transaction
        sqlite3pp::transaction tr(_libraryDb);
        // use lambda to be able to break out early if anything goes wrong
        bool success = [&]() -> bool {
            sqlite3pp::command cmd(
                _libraryDb,
                "insert into books (duration, author, name, series, description, path, thumbnail_path) values (?, ?, ?, ?, ?, ?, ?)");
            cmd.binder() << int64_t(bookInfo.duration) << bookInfo.author << bookInfo.name << bookInfo.series
                         << bookInfo.description << bookInfo.folder << bookInfo.thumbnailLocation;
            if (SQLITE_OK != cmd.execute())
            {
                return false;
            }

            int64_t bookId = _libraryDb.last_insert_rowid();

            for (const auto& media : bookInfo.files)
            {
                sqlite3pp::command cmd(
                    _libraryDb, "insert into files (book_id, last_modified, track_number, path) values (?, ?, ?, ?)");
                cmd.binder() << bookId << media.lastModified << (media.meta.trackNumber.empty() ? 0 : std::stoi(media.meta.trackNumber)) << media.path;
                if (SQLITE_OK != cmd.execute())
                {
                    return false;
                }
            }
            return true;
        }();  // notice the lambda being called
        if (success)
        {
            tr.commit();
        }
        else
        {
            tr.rollback();
        }
        return success;
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
        if (!_library.init(_vlcInstance))
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
