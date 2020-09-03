#pragma once

#include <string>

extern "C" {
typedef struct libvlc_instance_t libvlc_instance_t;
typedef struct libvlc_media_player_t libvlc_media_player_t;
}







struct BookInfo {

};

struct TrackInfo {

};

struct MetaInfo {

};


class Vlc
{
public:
    Vlc();
    ~Vlc();

    bool init(int argc , const char *const *argv);
private:
    libvlc_instance_t* _instance;
    libvlc_media_player_t *_mediaplayer;
};
