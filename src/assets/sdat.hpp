#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct HgSdatStats {
    bool valid=false;
    std::string error;
    std::size_t files=0,sequences=0,banks=0,waveArchives=0;
    std::size_t sequenceInfoCount=0,bankInfoCount=0,waveInfoCount=0;
};
struct HgSdatSequenceInfo {bool valid=false;std::uint16_t fileId=0,bankId=0;std::uint8_t volume=127,playerId=0;};
struct HgSbnkNoteRef {
    bool valid=false;
    std::uint16_t type=0;
    std::uint16_t waveId=0;
    std::uint16_t waveArchiveSlot=0;
    std::uint8_t rootKey=60;
};
// Decode the exact note definition selected by an SBNK program/key pair.
// Exposed for regression tests so split/range instrument layout cannot silently regress.
HgSbnkNoteRef hg_resolve_sbnk_note(const std::vector<unsigned char>& bankFile,std::size_t program,int note);
struct HgPcmWave {
    bool valid=false;
    std::string error;
    int sampleRate=0;
    bool loop=false;
    // Nintendo DS SWAV loop positions are stored separately from the sample
    // payload.  Keeping these boundaries prevents looped instruments from
    // jumping back to attack/transient data on every cycle.
    std::size_t loopStart=0;
    std::size_t loopEnd=0; // exclusive; 0 means samples.size()
    std::vector<std::int16_t> samples;
};

class HgSdat {
public:
    bool load(const std::filesystem::path& path);
    const HgSdatStats& stats() const{return stats_;}
    HgSdatSequenceInfo sequence(std::size_t id) const;
    HgPcmWave wave(std::size_t waveArchiveId,std::size_t waveId) const;
    // Returns one real SWAR sample associated with a sequence's SBNK. Kept as
    // a diagnostic/fallback helper; normal SE/fanfare playback uses renderSequence.
    HgPcmWave auditionSequenceSample(std::size_t sequenceId) const;
    // Native SSEQ renderer: interprets the original tracks/tempo/program selection
    // and mixes SBNK/SWAR instruments to PCM for the host audio device.
    HgPcmWave renderSequence(std::size_t sequenceId,double seconds=8.0) const;
private:
    std::vector<unsigned char> data_;
    HgSdatStats stats_;
    std::uint32_t infoOff_=0,fatOff_=0;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> files_;
    std::vector<std::uint32_t> seqEntry_,bankEntry_,swarEntry_;
    std::vector<unsigned char> fileBytes(std::size_t id) const;
    std::vector<std::uint32_t> infoEntries(int part) const;
};

class NativeAudio {
public:
    NativeAudio();~NativeAudio();
    NativeAudio(const NativeAudio&)=delete;NativeAudio& operator=(const NativeAudio&)=delete;
    bool ready() const{
#ifdef _WIN32
        return bgmWave_!=nullptr;
#else
        return bgmDevice_!=0;
#endif
    }
    // BGM and SFX are deliberately independent. Retail HG/SS mixes SE over the
    // field sequence; v0.13 cleared the single SDL queue for every sound effect.
    bool playBgm(const HgPcmWave& wave,float volume=0.45f,bool loop=true);
    bool playSfx(const HgPcmWave& wave,float volume=0.45f);
    bool play(const HgPcmWave& wave,float volume=0.45f){return playSfx(wave,volume);}
    void update();
    void pauseBgm();
    void resumeBgm();
    bool bgmPaused() const { return bgmPaused_; }
    void stopBgm();
    void stopSfx();
    void stop();
private:
    bool bgmPaused_=false;
    std::vector<std::int16_t> convert(const HgPcmWave& wave,float volume) const;
#ifdef _WIN32
    void* bgmWave_=nullptr;void* sfxWave_=nullptr;void* bgmHeader_=nullptr;void* sfxHeader_=nullptr;
    std::vector<std::int16_t> bgmPcm_,sfxPcm_;bool bgmLoop_=false;
#else
    void* lib_=nullptr;unsigned bgmDevice_=0,sfxDevice_=0;
    std::vector<std::int16_t> bgmPcm_;bool bgmLoop_=false;
    int (*initSub_)(std::uint32_t)=nullptr;
    void (*quitSub_)(std::uint32_t)=nullptr;
    unsigned (*openDev_)(const char*,int,const void*,void*,int)=nullptr;
    int (*queue_)(unsigned,const void*,std::uint32_t)=nullptr;
    std::uint32_t (*queuedSize_)(unsigned)=nullptr;
    void (*pause_)(unsigned,int)=nullptr;
    void (*clear_)(unsigned)=nullptr;
    void (*close_)(unsigned)=nullptr;
#endif
};
