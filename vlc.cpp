#include "vlc.h"
#include "vlc/libvlc.h"

Vlc::Vlc() { }

Vlc::~Vlc()
{
    if (_instance)
    {
        libvlc_release(_instance);
    }
}

bool Vlc::init(int argc, const char* const* argv)
{
    /* Load the VLC engine */
    _instance = libvlc_new(argc, argv);
    return _instance != nullptr;
}
