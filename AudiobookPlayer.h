#pragma once

#include <string>
#include <mutex>
#include <memory>

struct AudiobookPlayerImpl;


class AudiobookPlayer
{
public:
    AudiobookPlayer();
    ~AudiobookPlayer();

    void update();

    bool init(int argc , const char *const *argv);

private:
    std::unique_ptr<AudiobookPlayerImpl> _impl;
};
