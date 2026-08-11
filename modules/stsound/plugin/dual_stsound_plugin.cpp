// Adattatore: espone StSoundLibrary (sorgente upstream di Arnaud Carré, non
// distribuita come libreria pronta — a differenza di AdPlug/OpenMPT questo
// modulo COMPILA il sorgente, come dual-uade, ma senza fork: e' permissiva
// MIT, nessun isolamento legale necessario, solo coerenza architetturale)
// dietro l'ABI dual_audio_plugin.h.
//
// StSound sintetizza mono; l'espansione a stereo interleaved avviene qui,
// stessa scelta di stsound_player.cpp in Dual oggi. Nessun concetto di
// subsong per i file YM (chip Atari ST YM2149), come gia' annotato in
// stsound_backend.h.

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"

#include "StSoundLibrary.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ── Stato per istanza ────────────────────────────────────────────────────────

struct stsound_plugin_state_t {
    YMMUSIC* ym {nullptr};
    int rate {0};
    int playing {0};

    char title[256] {};
    char author[256] {};
    char last_error[256] {};

    std::vector<ymsample> mono_scratch;
};

static char g_create_error[256] = "";

static const char* const kExtensions[] = { "ym", nullptr };

// ── ops ──────────────────────────────────────────────────────────────────────

static void* DUAL_AUDIO_PLUGIN_ABI st_create(int preferred_rate_hz) {
    auto* s = new stsound_plugin_state_t();
    s->rate = preferred_rate_hz;
    return s;
}

static void DUAL_AUDIO_PLUGIN_ABI st_destroy(void* self) {
    auto* s = static_cast<stsound_plugin_state_t*>(self);
    if (s->ym) ymMusicDestroy(s->ym);
    delete s;
}

static const char* DUAL_AUDIO_PLUGIN_ABI st_last_error(void* self) {
    auto* s = static_cast<stsound_plugin_state_t*>(self);
    return s ? s->last_error : g_create_error;
}

static int DUAL_AUDIO_PLUGIN_ABI st_can_handle(void* self, const char* path) {
    (void)self;
    const char* dot = strrchr(path, '.');
    if (!dot || !dot[1]) return 0;
    const char* a = dot + 1;
    if ((a[0] == 'y' || a[0] == 'Y') && (a[1] == 'm' || a[1] == 'M') && !a[2])
        return 1;
    return 0;
}

static int DUAL_AUDIO_PLUGIN_ABI st_load(void* self, const char* path,
                                          dual_song_meta_t* out_meta) {
    auto* s = static_cast<stsound_plugin_state_t*>(self);

    if (s->ym) {
        ymMusicDestroy(s->ym);
        s->ym = nullptr;
    }

    YMMUSIC* ym = ymMusicCreateWithRate(s->rate);
    if (!ym) {
        snprintf(s->last_error, sizeof(s->last_error), "ymMusicCreateWithRate() failed");
        return 0;
    }
    ymMusicSetLoopMode(ym, YMFALSE);

    if (!ymMusicLoad(ym, path)) {
        snprintf(s->last_error, sizeof(s->last_error),
                 "ymMusicLoad() rejected: %s (%s)", path, ymMusicGetLastError(ym));
        ymMusicDestroy(ym);
        return 0;
    }

    ymMusicPlay(ym);
    s->ym = ym;
    s->playing = 1;

    ymMusicInfo_t raw{};
    ymMusicGetInfo(ym, &raw);

    s->title[0] = s->author[0] = '\0';
    if (raw.pSongName && raw.pSongName[0])
        strncpy(s->title, raw.pSongName, sizeof(s->title) - 1);
    if (raw.pSongAuthor && raw.pSongAuthor[0])
        strncpy(s->author, raw.pSongAuthor, sizeof(s->author) - 1);

    static char line1_buf[288];
    snprintf(line1_buf, sizeof(line1_buf), "Author: %s", s->author);

    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;

    out_meta->title           = s->title[0] ? s->title : nullptr;
    out_meta->line1           = s->author[0] ? line1_buf : "";
    out_meta->line2           = "";
    out_meta->line3           = "";
    out_meta->duration_s      = raw.musicTimeInMs > 0 ? raw.musicTimeInMs / 1000.0 : 0.0;
    out_meta->num_subsongs    = 1; // i file YM non hanno subsong
    out_meta->current_subsong = 0;

    return 1;
}

static void DUAL_AUDIO_PLUGIN_ABI st_pause(void* self)  { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI st_resume(void* self) { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI st_stop(void* self) {
    static_cast<stsound_plugin_state_t*>(self)->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI st_sample_rate(void* self) {
    return static_cast<stsound_plugin_state_t*>(self)->rate;
}

// StSound sintetizza mono: si espande a stereo interleaved qui, come faceva
// stsound_player.cpp — il canale L=R e' la stessa scelta del vecchio codice.
static int DUAL_AUDIO_PLUGIN_ABI st_render(void* self, int16_t* out, int frames) {
    auto* s = static_cast<stsound_plugin_state_t*>(self);
    if (!s->playing || !s->ym) return 0;

    if (static_cast<int>(s->mono_scratch.size()) < frames)
        s->mono_scratch.resize(frames);

    if (!ymMusicCompute(s->ym, s->mono_scratch.data(), frames)) {
        s->playing = 0;
        return 0;
    }

    for (int i = 0; i < frames; i++) {
        const auto v = static_cast<int16_t>(s->mono_scratch[i]);
        out[i * 2]     = v;
        out[i * 2 + 1] = v;
    }

    if (ymMusicIsOver(s->ym)) s->playing = 0;
    return frames;
}

static int DUAL_AUDIO_PLUGIN_ABI st_is_playing(void* self) {
    return static_cast<stsound_plugin_state_t*>(self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI st_is_paused(void* self) { (void)self; return 0; }

static void DUAL_AUDIO_PLUGIN_ABI st_set_volume(void* self, int pct) {
    (void)self; (void)pct;
}
static int DUAL_AUDIO_PLUGIN_ABI st_get_volume(void* self) { (void)self; return 100; }

// Nessun subsong nei file YM — no-op, come SetSubsong/NextSubsong/PrevSubsong
// gia' vuoti in stsound_backend.h oggi.
static void DUAL_AUDIO_PLUGIN_ABI st_set_subsong(void* self, int idx_0based,
                                                  dual_song_meta_t* out_meta) {
    (void)self; (void)idx_0based; (void)out_meta;
}

static double DUAL_AUDIO_PLUGIN_ABI st_get_position_seconds(void* self) {
    (void)self;
    return 0.0; // l'host calcola la posizione dai byte consumati
}

static int  DUAL_AUDIO_PLUGIN_ABI st_can_seek(void* self) { (void)self; return 0; }
static void DUAL_AUDIO_PLUGIN_ABI st_seek_seconds(void* self, double seconds) {
    (void)self; (void)seconds;
}

// ── Descrittore statico ───────────────────────────────────────────────────────

static const dual_audio_plugin_t kPlugin = {
    /* info */ {
        DUAL_AUDIO_PLUGIN_TYPE_GENERATOR,
        DUAL_AUDIO_PLUGIN_API_VMAJOR,
        DUAL_AUDIO_PLUGIN_API_VMINOR,
        1, 0,
        "stsound",
        "StSoundLibrary",
        "Atari ST YM2149 chiptune playback (.ym)",
        "StSoundLibrary\n"
        "Copyright (c) 2021 Arnaud Carre\n"
        "MIT License\n"
        "https://github.com/arnaud-carre/StSound",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        nullptr, // config_dialog
        0.0f,    // fade_in_seconds: usa il default di Dual
        nullptr, // extended_params
    },
    /* ops */ {
        st_create, st_destroy, st_last_error, st_can_handle,
        st_load, st_pause, st_resume, st_stop,
        st_sample_rate, st_render,
        st_is_playing, st_is_paused,
        st_set_volume, st_get_volume,
        st_set_subsong, st_get_position_seconds,
        st_can_seek, st_seek_seconds,
    },
};

extern "C" DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}
