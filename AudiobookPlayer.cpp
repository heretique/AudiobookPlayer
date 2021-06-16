#include "AudiobookPlayer.h"
#include "vlc/vlc.h"
#include "enkiTS/TaskScheduler.h"
#include "sqlite3pp/sqlite3pp.h"
#include "imgui.h"
#include "imFileBroser.h"
#include "imSpinner.h"
#include "StateMachine.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "uri.h"
#include "Hq/StringHash.h"
#include <glad/glad.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unordered_set>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace ui = ImGui;

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

// Fonts

static hq::StringHash kFontTitle       = "titleF"_sh;
static hq::StringHash kFontNormal      = "normalF"_sh;
static hq::StringHash kFontDescription = "descriptionF"_sh;

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

ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
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

ImVec2 scaleToFit(float imageAspectRatio, const ImVec2& availabeSpace)
{
    ImVec2 scaledSize;
    float  aspectRatio = availabeSpace.x / availabeSpace.y;
    if (aspectRatio > imageAspectRatio)
    {
        scaledSize.y = availabeSpace.y;
        scaledSize.x = availabeSpace.x * (imageAspectRatio / aspectRatio);
    }
    else
    {
        scaledSize.x = availabeSpace.x;
        scaledSize.y = availabeSpace.y / (imageAspectRatio / aspectRatio);
    }

    return scaledSize;
}

const char HEX2DEC[256] = {
    /*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
    /* 0 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 1 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 2 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 3 */ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1,

    /* 4 */ -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 5 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 6 */ -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 7 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 8 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 9 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* A */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* B */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* C */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* D */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* E */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* F */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

// STRUCTS & CLASSES

struct Texture
{
    unsigned int handle {0};
    float        aspectRatio {1};
};

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
    Texture            thumbnail;
};

struct Library
{
    enum class State {
        Idle,
        Working
    };

    State                                _state {State::Idle};
    sqlite3pp::database                  _libraryDb;
    std::unique_ptr<enki::TaskScheduler> _taskScheduler;
    std::unique_ptr<enki::TaskSet>       _currentTask;
    libvlc_instance_t*                   _vlcInstance {nullptr};  // shared VLC instance with the player
    std::vector<Book>                    _books;
    Texture                              _genericCover;

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

        _genericCover = loadImage("generic_cover.png");
        if (!_genericCover.handle)
        {
            return false;
        }

        readLibraryFromDb();

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

            for (auto& book : _books)
            {
                // make sure not empty
                if (book.files.size())
                {
                    resolveBookInfo(book);
                    writeBookToDb(book);
                }
            }
            _state = State::Idle;
        });
        _taskScheduler->AddTaskSetToPipe(_currentTask.get());
        _state = State::Working;

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

    void resolveBookInfo(Book& book)
    {

        if (!book.name.empty() && book.files.size() > 1)
        {
            if (book.files[0].meta.name == book.files[1].meta.name)
            {
                book.name = book.files[0].meta.name;
            }
        }

        // try get the name from meta info
        if (book.name.empty())
        {
            for (const auto& file : book.files)
            {
                if (!file.meta.name.empty())
                {
                    book.name = file.meta.name;
                    break;
                }
            }
        }

        // try get author from meta
        if (book.author.empty())
        {
            for (const auto& file : book.files)
            {
                if (!file.meta.author.empty())
                {
                    book.author = file.meta.author;
                    break;
                }
            }
        }

        // if not found in meta, get name from folder name
        if (book.name.empty())
        {
            fs::path bookFolder(book.folder);
            book.name = bookFolder.filename().string();
        }

        // try retrieving artwork from meta
        if (book.thumbnailLocation.empty())
        {
            for (const auto& file : book.files)
            {
                if (!file.meta.artworkUrl.empty())
                {
                    book.thumbnailLocation = file.meta.artworkUrl;
                    break;
                }
            }
        }

        // if not found in meta info try looking for an image inside folder
        if (book.thumbnailLocation.empty())
        {
        }

        for (const auto& file : book.files)
        {
            book.duration += file.duration;
        }
    }

    // https://stackoverflow.com/questions/18307429/encode-decode-url-in-c/35348028
    // https://www.codeguru.com/cpp/cpp/algorithms/strings/article.php/c12759/URI-Encoding-and-Decoding.htm
    std::string UriDecode(const std::string& sSrc)
    {
        // Note from RFC1630: "Sequences which start with a percent
        // sign but are not followed by two hexadecimal characters
        // (0-9, A-F) are reserved for future extension"

        const unsigned char*       pSrc    = (const unsigned char*)sSrc.c_str();
        const int                  SRC_LEN = sSrc.length();
        const unsigned char* const SRC_END = pSrc + SRC_LEN;
        // last decodable '%'
        const unsigned char* const SRC_LAST_DEC = SRC_END - 2;

        char* const pStart = new char[SRC_LEN];
        char*       pEnd   = pStart;

        while (pSrc < SRC_LAST_DEC)
        {
            if (*pSrc == '%')
            {
                char dec1, dec2;
                if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)]) && -1 != (dec2 = HEX2DEC[*(pSrc + 2)]))
                {
                    *pEnd++ = (dec1 << 4) + dec2;
                    pSrc += 3;
                    continue;
                }
            }

            *pEnd++ = *pSrc++;
        }

        // the last 2- chars
        while (pSrc < SRC_END)
            *pEnd++ = *pSrc++;

        std::string sResult(pStart, pEnd);
        delete[] pStart;
        return sResult;
    }

    Texture loadImage(const std::string& filename)
    {
        Texture     texture;
        std::string path;
        if (filename.find("file:///") != std::string::npos)
        {
            uri fileUri(filename);
            path = UriDecode(fileUri.get_path());
        }
        else
        {
            path = filename;
        }

        int width, height, channels;

        unsigned char* imageData = stbi_load(path.c_str(), &width, &height, &channels, 0);

        if (imageData)
        {
            glGenTextures(1, &texture.handle);
            glBindTexture(GL_TEXTURE_2D, texture.handle);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            int internalFormat = GL_RGBA;
            int format = GL_RGBA;
            int unpackAlignment = 4;
            if (channels == 3)
            {
                internalFormat = GL_RGB;
                format = GL_RGB;
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, imageData);
            if (channels == 3)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
            }
            stbi_image_free(imageData);

            texture.aspectRatio = float(width) / height;
        }

        return texture;
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
                cmd.binder() << bookId << media.lastModified
                             << (media.meta.trackNumber.empty() ? 0 : std::stoi(media.meta.trackNumber)) << media.path;
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

    void readLibraryFromDb()
    {
        _books.clear();
        sqlite3pp::query query(_libraryDb, "select * from books");
        for (sqlite3pp::query::iterator i = query.begin(); i != query.end(); ++i)
        {
            Book book;
            std::tie(book.id, book.duration, book.author, book.name, book.series, book.description, book.folder,
                     book.thumbnailLocation) =
                (*i).get_columns<long, long long, char const*, char const*, char const*, char const*, char const*,
                                 char const*>(0, 1, 2, 3, 4, 5, 6, 7);
            if (!book.thumbnailLocation.empty())
            {
                book.thumbnail = loadImage(book.thumbnailLocation);
            }

            if (!book.thumbnail.handle)
            {
                book.thumbnail = _genericCover;
            }

            _books.emplace_back(book);
        }
    }

};  // struct Library

struct AudiobookPlayerImpl
{
    using SM = StateMachine<PlayerState>;

    libvlc_instance_t*                          _vlcInstance {nullptr};
    libvlc_media_player_t*                      _mediaPlayer {nullptr};
    libvlc_media_t*                             _currentMedia {nullptr};
    SM                                          _stateMachine;
    Library                                     _library;
    std::unique_ptr<Book>                       _currentBook;
    std::string                                 _status;
    std::unordered_map<hq::StringHash, ImFont*> _fonts;

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
            [this]() { this->onExitLibraryDiscovery(); });
        _stateMachine.addState(
            PlayerState::Player,
            [this]() { return this->onEnterPlayer(); },
            [this]() { return this->onUpdatePlayer(); },
            []() {});
        _stateMachine.addState(
            PlayerState::Library,
            [this]() { return this->onEnterLibrary(); },
            [this]() { return this->onUpdateLibrary(); },
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
        if (!initFonts())
            return false;

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

    bool initFonts()
    {
        ImFontAtlas* fontAtlas = ui::GetIO().Fonts;
        ImFont*      font      = fontAtlas->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 16.f);
        if (!font)
            return false;
        _fonts.emplace(std::make_pair(kFontNormal, font));

        font = fontAtlas->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 24.f);
        if (!font)
            return false;
        _fonts.emplace(std::make_pair(kFontTitle, font));

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
        drawToolbar();
        _stateMachine.tick();
        drawStatus();
    }

    void drawToolbar()
    {
        ui::Text("Toolbar here...");
        ui::Separator();
    }

    void drawStatus()
    {
        ui::SetCursorPosX(0);
        ui::SetCursorPosY(ui::GetWindowHeight() - 2 * ui::GetFontSize());
        ui::Separator();
        ui::Text(_status.c_str());
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
            if (_currentBook)
            {
                return PlayerState::Player;
            }
            else
            {
                return PlayerState::Library;
            }
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

        if (ui::Button("Choose library location"))
        {
            showDialog = true;
        }

        if (showDialog &&
            ui::FileBrowser(kChooseLibraryLocation, location, showDialog, ImGuiFileBrowserFlags_SelectDirectory))
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
        ui::SetCursorPos((ui::GetWindowSize() - 200.f) / 2.0f);
        ui::SpinnerCircle("Library Discovery...", 100.f,
                             ui::ColorConvertU32ToFloat4(ui::GetColorU32(ImGuiCol_ButtonHovered)),
                             ui::ColorConvertU32ToFloat4(ui::GetColorU32(ImGuiCol_FrameBg)), 16, 2.0f);

        if (_library._state != Library::State::Working)
        {
            return PlayerState::Library;
        }

        return {};
    }
    void onExitLibraryDiscovery() {
        _library.readLibraryFromDb();
    }

    // PlayerState::LibraryParsing
    void onEnterLibraryParsing() { }
    void onUpdateLibraryParsing() { }
    void onExitLibraryParsing() { }

    // PlayerState::Settings
    void onEnterSettings() { }
    void onUpdateSettings() { }
    void onExitSettings() { }

    // PlayerState::Library
    SM::ResultType onEnterLibrary()
    {
        return {};
    }
    SM::ResultType onUpdateLibrary()
    {
        static size_t selectedIndex = 0;
        float         listBoxHeight = ui::GetWindowHeight() - 2 * ui::GetCursorPosY();
        float         listBoxWidth  = ui::GetWindowWidth() / 2 - ui::GetStyle().FramePadding.x;
        if (ui::BeginChild("content"))
        {
            ui::Columns(2);
            if (ui::ListBoxHeader("##", ImVec2(listBoxWidth, listBoxHeight)))
            {
                int count = 0;
                for (const auto& book : _library._books)
                {
                    if (ui::Selectable(book.name.c_str(), count == selectedIndex, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        selectedIndex = count;
                    }
                    count++;
                }
                ui::ListBoxFooter();
                if (selectedIndex >= _library._books.size())
                {
                    selectedIndex = 0;
                }
            }
            ui::NextColumn();
            const Book& selectedBook = _library._books[selectedIndex];
            if (selectedBook.thumbnail.handle)
            {
                ImVec2 imageSpace(listBoxWidth, listBoxHeight / 2.f);
                ImVec2 imageSize = scaleToFit(selectedBook.thumbnail.aspectRatio, imageSpace);
                ImVec2 cursorPos = ui::GetCursorPos();
                ui::SetCursorPos(cursorPos + (imageSpace - imageSize) / 2);
                ui::Image((void*)(intptr_t)selectedBook.thumbnail.handle, imageSize);
            }

            ui::NewLine();

            ui::PushFont(_fonts[kFontTitle]);
            ui::SetCursorPosX(ui::GetCursorPosX() +
                              (listBoxWidth - ui::CalcTextSize(selectedBook.name.c_str()).x) / 2.f);
            ui::Text(selectedBook.name.c_str());
            ui::PopFont();
            ui::SetCursorPosX(ui::GetCursorPosX() +
                              (listBoxWidth - ui::CalcTextSize(selectedBook.author.c_str()).x) / 2.f);
            ui::Text(selectedBook.author.c_str());
            std::stringstream ss;
            ss << "Duration: " << selectedBook.duration;
            ui::SetCursorPosX(ui::GetCursorPosX() +
                              (listBoxWidth - ui::CalcTextSize(ss.str().c_str()).x) / 2.f);
            ui::Text(ss.str().c_str());
            ui::EndChild();
        }

        return {};
    }
    void onExitLibrary() { }

    // PlayerState::BookInfo
    void onEnterBookInfo() { }
    void onUpdateBookInfo() { }
    void onExitBookInfo() { }

    // PlayerState::Player
    SM::ResultType onEnterPlayer()
    {
        return {};
    }
    SM::ResultType onUpdatePlayer()
    {
        return {};
    }
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
