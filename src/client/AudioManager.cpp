#include "AudioManager.h"
#include <SDL2/SDL_mixer.h>
#include <algorithm>
#include <cstdint>

namespace dz {

bool AudioManager::init(int frequency, int channels, int chunkSize) {
    return Mix_OpenAudio(frequency, MIX_DEFAULT_FORMAT, channels, chunkSize) == 0;
}

void AudioManager::shutdown() {
    for (auto& [k, chunk] : m_sounds)
        if (chunk) Mix_FreeChunk(chunk);
    m_sounds.clear();
    Mix_CloseAudio();
}

bool AudioManager::loadSound(const std::string& key, const std::string& path) {
    Mix_Chunk* c = Mix_LoadWAV(path.c_str());
    if (!c) return false;
    m_sounds[key] = c;
    return true;
}

int AudioManager::playSound(const std::string& key, float volume, bool loop) {
    auto it = m_sounds.find(key);
    if (it == m_sounds.end()) return -1;
    int vol = static_cast<int>(std::clamp(volume * m_masterVolume, 0.0f, 1.0f)
                               * MIX_MAX_VOLUME);
    Mix_VolumeChunk(it->second, vol);
    return Mix_PlayChannel(-1, it->second, loop ? -1 : 0);
}

void AudioManager::stopChannel(int channel) {
    Mix_HaltChannel(channel);
}

void AudioManager::setMasterVolume(float v) {
    m_masterVolume = std::clamp(v, 0.0f, 1.0f);
}

} // namespace dz
