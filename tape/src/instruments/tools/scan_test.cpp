#include "../soundlibrary.h"
#include <cstdio>
int main(int argc, char** argv)
{
    using namespace au::instruments;
    const std::string folder = argc > 1 ? argv[1] : defaultSoundsFolder();
    for (const SoundEntry& e : scanSounds(folder)) {
        std::printf("%-13s %-24s %s\n", familyName(e.family), e.library.c_str(), e.name.c_str());
    }
}
