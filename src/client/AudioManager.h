#pragma once
#include <string>
#include <unordered_map>

struct Mix_Chunk;

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// AudioManager  —  wraps SDL2_mixer
//
// Sounds are keyed by string (matches SoundEmitterComponent::pendingSoundKey).
// Positional audio is approximated by volume scaling based on
// distance from the local player.
// ─────────────────────────────────────────────────────────────────────────────
class AudioManager {
public:
    bool init(int frequency = 44100, int channels = 2, int chunkSize = 2048);
    void shutdown();

    bool loadSound(const std::string& key, const std::string& path);
    int  playSound(const std::string& key, float volume = 1.0f, bool loop = false);
    void stopChannel(int channel);
    void setMasterVolume(float v);  ///< [0, 1]

private:
    std::unordered_map<std::string, Mix_Chunk*> m_sounds;
    float m_masterVolume = 1.0f;
};

} // namespace dz
