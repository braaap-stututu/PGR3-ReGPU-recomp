// XMP, the console's media player (XMsgInProcessCall app 0xFA), for PGR3's own music (session 6, PGR3-QUIRKS A4).
//
// PGR3 plays its soundtrack (Game/Audio/music/*.wma: ASF files with WMA v2) through the media player: it builds a
// title playlist from the files and their names, asks the player to play it, and registers a capture callback
// (XMPCaptureOutput) that receives the decoded audio, which the game mixes into its own voice. While a song plays the
// game asks for it (XMPGetCurrentSong) and shows the "now playing" panel (song, artist, label) in the countdown.
// Without this the player answered "idle" to everything: no music and no panel.
//
// Here: playlists are kept as the game gives them; one host thread decodes the current song with FFmpeg (avformat +
// the wmav2 decoder, resampled to 48 kHz stereo) into a ring; one guest thread hands the callback 256 frames every
// 5.33 ms as big-endian 32-bit floats, interleaved L R (the layout the PGR4 port measured for the same XDK player),
// from a pool of buffers (the game's voice keeps the pointer until it has played it).
// Message ids and argument layouts follow Xenia's xmp_app (the PGR4 port checked them against a game).
//
// PGR3_NO_XMP=1: not used (the old answers in kernel/imports_xam.cpp). PGR3_TRACE_XMP=1: every message and song.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include "kernel/imports.h"
#include "kernel/thread.h"
#include "kernel/vfs.h"
#include "log.h"
#include "memory.h"

void BroadcastNotification(uint32_t id, uint32_t param); // kernel/imports_xam.cpp

namespace
{
constexpr uint32_t kOk = 0, kFail = 0x80004005, kInvalidArg = 0x80070057;
constexpr uint32_t kXnStateChanged = 0x0A000001; // XN_XMP_STATECHANGED
constexpr uint32_t kRate = 48000, kBlockFrames = 256, kBlockBytes = kBlockFrames * 2 * 4, kPoolBlocks = 64;
constexpr size_t kRingFrames = size_t(kRate) * 2; // decode up to two seconds ahead

enum class State : uint32_t { Idle = 0, Playing = 1, Paused = 2 };

struct Song
{
    uint32_t handle = 0;
    std::string path;               // guest path
    std::u16string text[5];         // title, artist, album, album artist, genre
    uint32_t track = 0, duration = 0, format = 0;
};

struct Playlist
{
    std::u16string name;
    std::vector<Song> songs;
};

bool Trace()
{
    static const bool on = getenv("PGR3_TRACE_XMP") != nullptr;
    return on;
}

std::u16string ReadWide(uint32_t address, size_t limit)
{
    std::u16string s;
    for (size_t i = 0; address && i < limit; i++)
    {
        const char16_t c = char16_t(__builtin_bswap16(*GuestPtr<uint16_t>(address + uint32_t(i) * 2)));
        if (!c)
            break;
        s += c;
    }
    return s;
}

std::string Utf8(const std::u16string& w)
{
    std::string s;
    for (char16_t c : w)
    {
        if (c < 0x80)
            s += char(c);
        else if (c < 0x800)
            s += char(0xC0 | (c >> 6)), s += char(0x80 | (c & 0x3F));
        else
            s += char(0xE0 | (c >> 12)), s += char(0x80 | ((c >> 6) & 0x3F)), s += char(0x80 | (c & 0x3F));
    }
    return s;
}

uint32_t Arg(uint32_t args, uint32_t i) { return LoadBE32(args + i * 4); }

// The player. Everything below is under m.
struct Player
{
    std::mutex m;
    std::condition_variable wake;
    std::map<uint32_t, Playlist> playlists;
    uint32_t nextPlaylist = 0xB1000001u, nextSong = 0xB2000001u;
    State state = State::Idle;
    uint32_t playing = 0;            // playlist handle
    std::vector<size_t> order;       // song indices in playing order
    size_t position = 0;
    uint32_t mode = 0, repeat = 0, flags = 0; // mode 1 shuffle; repeat 1 = stop at the end
    float volume = 1.0f;
    uint32_t controller = 0, controllerLocked = 0;
    uint32_t song = 0;               // bumped whenever the song to decode changes
    std::vector<float> ring;         // decoded frames, interleaved stereo, consumed from ringRead
    size_t ringRead = 0;
    uint32_t callback = 0, callbackContext = 0, titleRender = 0;
    bool notifyDue = false;          // XN_XMP_STATECHANGED once the decoder has the song open
};
Player g_p;
std::once_flag g_threadsStarted;
std::shared_ptr<ThreadObject> g_captureThread;

size_t Buffered() { return (g_p.ring.size() - g_p.ringRead) / 2; }

void ClearRing()
{
    g_p.ring.clear();
    g_p.ringRead = 0;
}

const Song* Current()
{
    const auto it = g_p.playlists.find(g_p.playing);
    if (it == g_p.playlists.end() || g_p.position >= g_p.order.size() || g_p.order[g_p.position] >= it->second.songs.size())
        return nullptr;
    return &it->second.songs[g_p.order[g_p.position]];
}

void Restart()
{
    ClearRing();
    g_p.song++;
    g_p.notifyDue = true;
    g_p.wake.notify_all();
}

// Moves `step` songs on; false at the end of a playlist that doesn't repeat.
bool Step(int step)
{
    if (g_p.order.empty())
        return false;
    const int64_t next = int64_t(g_p.position) + step;
    if (next < 0)
        g_p.position = g_p.order.size() - 1;
    else if (size_t(next) >= g_p.order.size())
    {
        if (g_p.repeat == 1)
            return false;
        g_p.position = 0;
    }
    else
        g_p.position = size_t(next);
    return true;
}

// Host thread: decodes the current song into the ring.
void DecodeLoop()
{
    SetThreadDescription(GetCurrentThread(), L"pgr3 xmp decoder");
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int stream = -1;
    uint32_t openSong = ~0u;
    std::vector<float> converted;
    const auto close = [&] {
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        stream = -1;
    };
    for (;;)
    {
        std::string path;
        uint32_t song;
        {
            std::unique_lock lock(g_p.m);
            g_p.wake.wait_for(lock, std::chrono::milliseconds(50), [] { return g_p.state == State::Playing && Buffered() < kRingFrames; });
            if (g_p.state != State::Playing || Buffered() >= kRingFrames)
            {
                if (g_p.state == State::Idle && format)
                    close();
                continue;
            }
            song = g_p.song;
            if (song != openSong || !format)
            {
                close();
                openSong = song;
                const Song* s = Current();
                if (!s)
                {
                    g_p.state = State::Idle;
                    continue;
                }
                path = s->path;
            }
        }
        if (!path.empty())
        {
            std::filesystem::path host;
            bool readOnly = false;
            bool ok = VfsResolve(path, host, &readOnly) &&
                avformat_open_input(&format, reinterpret_cast<const char*>(host.u8string().c_str()), nullptr, nullptr) >= 0 &&
                avformat_find_stream_info(format, nullptr) >= 0;
            const AVCodec* decoder = nullptr;
            if (ok)
            {
                stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
                ok = stream >= 0 && decoder && (codec = avcodec_alloc_context3(decoder)) &&
                    avcodec_parameters_to_context(codec, format->streams[stream]->codecpar) >= 0 && avcodec_open2(codec, decoder, nullptr) >= 0;
            }
            if (ok)
            {
                AVChannelLayout stereo{};
                av_channel_layout_default(&stereo, 2);
                ok = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_FLT, int(kRate), &codec->ch_layout, codec->sample_fmt,
                         codec->sample_rate, 0, nullptr) >= 0 && swr_init(resampler) >= 0;
            }
            if (Trace() || !ok)
                Log("xmp: %s %s", ok ? "playing" : "can't open", path.c_str());
            std::lock_guard lock(g_p.m);
            if (!ok)
            {
                close();
                if (g_p.song == openSong && !Step(1))
                    g_p.state = State::Idle;
                else if (g_p.song == openSong)
                    Restart();
                continue;
            }
            if (g_p.notifyDue && g_p.song == openSong)
            {
                // The state change once the song is open (as Xenia's player): the game then asks which song it is. Sent
                // from here, not from inside the game's own request (PGR4: its listener ran while the game was still
                // setting the playlist up).
                g_p.notifyDue = false;
                BroadcastNotification(kXnStateChanged, uint32_t(g_p.state));
            }
        }
        if (av_read_frame(format, packet) < 0)
        {
            close();
            std::lock_guard lock(g_p.m);
            if (g_p.song == openSong)
            {
                if (Step(1))
                    Restart();
                else
                {
                    g_p.state = State::Idle;
                    BroadcastNotification(kXnStateChanged, uint32_t(g_p.state));
                }
                if (Trace())
                    Log("xmp: song ended (%s)", g_p.state == State::Idle ? "playlist ended" : "next");
            }
            continue;
        }
        if (packet->stream_index == stream && avcodec_send_packet(codec, packet) >= 0)
            while (avcodec_receive_frame(codec, frame) == 0)
            {
                const int room = swr_get_out_samples(resampler, frame->nb_samples);
                converted.resize(size_t(std::max(room, 0)) * 2);
                uint8_t* out = reinterpret_cast<uint8_t*>(converted.data());
                const int n = swr_convert(resampler, &out, room, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                av_frame_unref(frame);
                if (n <= 0)
                    continue;
                std::lock_guard lock(g_p.m);
                if (g_p.song != openSong)
                    break;
                if (g_p.ringRead > (1u << 20))
                {
                    g_p.ring.erase(g_p.ring.begin(), g_p.ring.begin() + ptrdiff_t(g_p.ringRead));
                    g_p.ringRead = 0;
                }
                g_p.ring.insert(g_p.ring.end(), converted.begin(), converted.begin() + ptrdiff_t(n) * 2);
            }
        av_packet_unref(packet);
    }
}

// Guest thread: hands the game's callback one block per 5.33 ms while a song plays (silence where the decoder is behind).
void CaptureLoop(PPCContext& ctx)
{
    using Clock = std::chrono::steady_clock;
    const uint32_t pool = GuestAlloc(Heap::Virtual, kBlockBytes * kPoolBlocks, 128);
    Log("xmp: capture blocks at %08X (%u x %u bytes)", pool, kPoolBlocks, kBlockBytes);
    uint32_t block = 0;
    auto due = Clock::now();
    uint32_t delivered = 0, starved = 0;
    auto reported = Clock::now();
    for (;;)
    {
        due += std::chrono::microseconds(1000000ull * kBlockFrames / kRate);
        std::this_thread::sleep_until(due);
        if (Clock::now() - due > std::chrono::milliseconds(200))
            due = Clock::now(); // stalled: no burst to catch up
        const uint32_t buffer = pool + kBlockBytes * (block++ % kPoolBlocks);
        uint32_t callback = 0, context = 0;
        {
            std::lock_guard lock(g_p.m);
            if (g_p.state != State::Playing || !g_p.callback)
                continue;
            callback = g_p.callback;
            context = g_p.callbackContext;
            uint32_t* out = GuestPtr<uint32_t>(buffer);
            const size_t have = std::min<size_t>(Buffered(), kBlockFrames);
            starved += have < kBlockFrames;
            for (uint32_t i = 0; i < kBlockFrames * 2; i++)
            {
                // With title render (XMPCaptureOutput's last argument) the game plays the music through its own voice
                // at its own volume: the player's volume is for the player's own output, and PGR3 sets it to 0 at boot
                // (applied here, every block was silent). Without title render it scales, as the player's output would.
                const float gain = g_p.titleRender ? 1.0f : g_p.volume;
                const float v = i < have * 2 ? g_p.ring[g_p.ringRead + i] * gain : 0.0f;
                uint32_t bits;
                memcpy(&bits, &v, 4);
                out[i] = __builtin_bswap32(bits);
            }
            g_p.ringRead += have * 2;
            g_p.wake.notify_all();
        }
        ctx.r3.u64 = buffer;
        ctx.r4.u64 = context;
        GuestCall(ctx, callback);
        delivered++;
        if (Trace() && Clock::now() - reported >= std::chrono::seconds(5))
        {
            Log("xmp: %u blocks to the game in 5 s (%u short of decoded audio)", delivered, starved);
            delivered = starved = 0;
            reported = Clock::now();
        }
    }
}

void StartThreads()
{
    std::call_once(g_threadsStarted, [] {
        std::thread(DecodeLoop).detach();
        g_captureThread = CreateHostGuestThread("pgr3 xmp capture", CaptureLoop);
    });
}

// A playlist by its handle, or by a pointer to it (the handle the game holds is its own buffer with ours inside).
uint32_t FindPlaylist(uint32_t value)
{
    if (g_p.playlists.count(value))
        return value;
    if (value >= 0x10000 && g_p.playlists.count(LoadBE32(value)))
        return LoadBE32(value);
    return 0;
}

void StoreFloat(uint32_t address, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    StoreIfNonNull(address, bits);
}
}

// One message (args: big-endian dwords, the client first). Called by XMsgInProcessCall (kernel/imports_xam.cpp).
uint32_t XmpMessage(uint32_t message, uint32_t args, uint32_t length)
{
    std::unique_lock lock(g_p.m);
    if (Trace())
        Log("xmp: message %08X args %08X %08X %08X %08X", message, Arg(args, 0), Arg(args, 1), Arg(args, 2), Arg(args, 3));
    switch (message)
    {
    case 0x00070009: // XMPGetStatus (client, state*)
        StoreIfNonNull(Arg(args, 1), uint32_t(g_p.state));
        return kOk;
    case 0x0007000B: // XMPGetVolume (client, float*)
        StoreFloat(Arg(args, 1), g_p.volume);
        return kOk;
    case 0x0007000C: // XMPSetVolume (client, float)
    {
        const uint32_t bits = Arg(args, 1);
        float v;
        memcpy(&v, &bits, 4);
        if (!(v >= 0.0f && v <= 1.0f))
            return kInvalidArg;
        g_p.volume = v;
        return kOk;
    }
    case 0x0007001B: // XMPGetPlaybackController (client, controller*, locked*)
        // Leaving them unwritten read as "the player belongs to the user's own soundtrack" (FM4 faded its music out).
        StoreIfNonNull(Arg(args, 1), g_p.controller);
        StoreIfNonNull(Arg(args, 2), g_p.controllerLocked);
        return kOk;
    case 0x0007001A: // XMPSetPlaybackController (client, controller, locked); 4 = give it back
        g_p.controller = Arg(args, 1) == 4 ? 0 : Arg(args, 1);
        g_p.controllerLocked = Arg(args, 2);
        return kOk;
    case 0x00070008: // XMPSetPlaybackBehavior (client, mode, repeat, flags)
        g_p.mode = Arg(args, 1), g_p.repeat = Arg(args, 2), g_p.flags = Arg(args, 3);
        return kOk;
    case 0x00070029: // XMPGetPlaybackBehavior (client, mode*, repeat*, flags*)
        StoreIfNonNull(Arg(args, 1), g_p.mode);
        StoreIfNonNull(Arg(args, 2), g_p.repeat);
        StoreIfNonNull(Arg(args, 3), g_p.flags);
        return kOk;
    case 0x0007003D: // XMPCaptureOutput (client, callback, context, title render)
        g_p.callback = Arg(args, 1);
        g_p.callbackContext = Arg(args, 2);
        g_p.titleRender = Arg(args, 3);
        Log("xmp: the game takes the music through its callback %08X (context %08X, title render %u)", g_p.callback, g_p.callbackContext, g_p.titleRender);
        lock.unlock();
        StartThreads();
        return kOk;
    case 0x0007002E: // XMPGetTitlePlaylistBufferSize (client, songs, size*)
        StoreIfNonNull(Arg(args, 2), Arg(args, 1) * 0x3E8 + 0x88);
        return kOk;
    case 0x0007000D: // XMPCreateTitlePlaylist (client, storage, ?, songs*, count, name*, flags, song handles*, playlist*)
    {
        Playlist list;
        list.name = ReadWide(Arg(args, 5), 260);
        const uint32_t songs = Arg(args, 3), count = std::min<uint32_t>(Arg(args, 4), 1000), handles = Arg(args, 7);
        for (uint32_t i = 0; i < count; i++)
        {
            // XMP_SONGDESCRIPTOR: path, title, artist, album, album artist, genre, track, duration, format.
            const uint32_t d = songs + i * 36;
            Song s;
            s.path = Utf8(ReadWide(LoadBE32(d), 260));
            for (uint32_t k = 0; k < 5; k++)
                s.text[k] = ReadWide(LoadBE32(d + 4 + k * 4), 39);
            s.track = LoadBE32(d + 24), s.duration = LoadBE32(d + 28), s.format = LoadBE32(d + 32);
            s.handle = g_p.nextSong++;
            if (handles)
                StoreBE32(handles + i * 4, s.handle);
            list.songs.push_back(std::move(s));
        }
        const uint32_t handle = g_p.nextPlaylist++;
        // The game gets its own storage buffer back as the playlist, our handle in its first word (as Xenia): it later
        // frees that pointer with its allocator (PGR4: our bare number there broke its heap).
        if (const uint32_t storage = Arg(args, 1))
        {
            StoreBE32(storage, handle);
            StoreIfNonNull(Arg(args, 8), storage);
        }
        else
            StoreIfNonNull(Arg(args, 8), handle);
        Log("xmp: playlist '%s': %zu songs%s%s", Utf8(list.name).c_str(), list.songs.size(), list.songs.empty() ? "" : ", first ",
            list.songs.empty() ? "" : list.songs[0].path.c_str());
        g_p.playlists[handle] = std::move(list);
        return kOk;
    }
    case 0x00070013: // XMPDeleteTitlePlaylist (client, playlist)
    {
        const uint32_t handle = FindPlaylist(Arg(args, 1));
        if (handle && handle == g_p.playing)
        {
            g_p.state = State::Idle;
            g_p.playing = 0;
            g_p.order.clear();
            ClearRing();
            g_p.song++;
        }
        g_p.playlists.erase(handle);
        return kOk;
    }
    case 0x00070031: // XMPGetNumSongsInTitlePlaylist (client, playlist, count*)
    {
        const auto it = g_p.playlists.find(FindPlaylist(Arg(args, 1)));
        StoreIfNonNull(Arg(args, 2), it == g_p.playlists.end() ? 0 : uint32_t(it->second.songs.size()));
        return kOk;
    }
    case 0x00070002: // XMPPlayTitlePlaylist (client, playlist, first song handle)
    {
        const uint32_t handle = FindPlaylist(Arg(args, 1));
        const auto it = g_p.playlists.find(handle);
        if (it == g_p.playlists.end() || it->second.songs.empty())
            return kFail;
        const std::vector<Song>& songs = it->second.songs;
        size_t first = 0;
        for (size_t i = 0; i < songs.size(); i++)
            if (songs[i].handle == Arg(args, 2))
                first = i;
        g_p.order.resize(songs.size());
        for (size_t i = 0; i < songs.size(); i++)
            g_p.order[i] = i;
        if (g_p.mode == 1)
        {
            static std::mt19937 rng{ std::random_device{}() };
            std::shuffle(g_p.order.begin(), g_p.order.end(), rng);
        }
        std::iter_swap(g_p.order.begin(), std::find(g_p.order.begin(), g_p.order.end(), first));
        g_p.position = 0;
        g_p.playing = handle;
        g_p.state = State::Playing;
        Restart();
        lock.unlock();
        StartThreads();
        return kOk;
    }
    case 0x00070003: // XMPContinue
        if (g_p.state == State::Paused)
            g_p.state = State::Playing;
        g_p.wake.notify_all();
        return kOk;
    case 0x00070005: // XMPPause
        if (g_p.state == State::Playing)
            g_p.state = State::Paused;
        return kOk;
    case 0x00070004: // XMPStop (no notification: the game asked, and may be tearing its playlists down)
        g_p.state = State::Idle;
        ClearRing();
        g_p.song++;
        return kOk;
    case 0x00070006: // XMPNext
    case 0x00070007: // XMPPrevious
        if (g_p.state != State::Idle && Step(message == 0x00070006 ? 1 : -1))
            Restart();
        return kOk;
    case 0x0007000E: // XMPGetCurrentSong (client, ?, XMP_SONGINFO*)
    {
        // XMP_SONGINFO (0x3E0 bytes): the song's handle, then at 0x240 title, artist, album, album artist and genre (40
        // UTF-16 characters each), track, duration, format. The countdown's "now playing" panel shows these.
        const Song* s = g_p.state == State::Idle ? nullptr : Current();
        const uint32_t info = Arg(args, 2);
        if (!s || !info)
            return kFail;
        memset(GuestPtr<uint8_t>(info), 0, 0x3E0);
        StoreBE32(info, s->handle);
        for (uint32_t k = 0; k < 5; k++)
            for (size_t n = 0; n < s->text[k].size() && n < 39; n++)
                *GuestPtr<uint16_t>(info + 0x240 + k * 80 + uint32_t(n) * 2) = __builtin_bswap16(uint16_t(s->text[k][n]));
        StoreBE32(info + 0x3D0, s->track);
        StoreBE32(info + 0x3D4, s->duration);
        StoreBE32(info + 0x3D8, s->format);
        if (Trace())
            Log("xmp: current song '%s' by '%s'", Utf8(s->text[0]).c_str(), Utf8(s->text[1]).c_str());
        return kOk;
    }
    default:
        LOG_ONCE("xmp: message %08X (%u bytes) answered with success", message, length);
        return kOk;
    }
}
