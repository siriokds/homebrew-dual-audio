// Adattatore: espone libsidplayfp dietro l'ABI dual_audio_plugin.h.
// Nessun fork qui (a differenza di dual-uade): libsidplayfp upstream basta,
// il problema che questo modulo risolve e' solo isolare il suo codice GPL
// dal binario di Dual, non aggiungere formati.
//
// Logica di riproduzione rispecchia sidplayfp_player.cpp in Dual — stessa
// sequenza di chiamate (sidplayfp/SidTune/ReSIDfpBuilder/SidConfig), solo
// dietro la vtable generica invece che dentro Dual stesso. I SID non hanno
// mai fine canzone (loop indefinito): render() ritorna sempre i frame
// richiesti, mai 0/negativo per "fine".

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"

// Suppress "deprecated" per la play(short*,count) API — stessa scelta
// deliberata di sidplayfp_player.cpp, funziona in 2.x.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/builders/residfp.h>

#include <cstdio>
#include <cstring>
#include <memory>

// ── Stato per istanza ────────────────────────────────────────────────────────

struct sidplayfp_plugin_state_t {
    std::unique_ptr<sidplayfp>      engine;
    std::unique_ptr<ReSIDfpBuilder> rs;
    std::unique_ptr<SidTune>        tune;

    int  rate {0};
    int  playing {0};
    int  current_subsong {1}; // 1-based, come l'API sidplayfp
    int  num_subsongs {1};

    char title[256] {};
    char author[256] {};
    char released[128] {};
    char last_error[256] {};
};

static char g_create_error[256] = "";

static const char* const kExtensions[] = { "sid", "psid", "rsid", nullptr };

// Nessun parametro configurabile oggi: il ricampionatore band-limited e'
// sempre attivo (motivato in sidplayfp_player.cpp — l'aliasing sopra Nyquist
// e' udibile su onde ricche di armoniche come quelle del SID), non ha senso
// renderlo spegnibile dall'utente.
static const char* const kConfigDialog = nullptr;

// ── ops ──────────────────────────────────────────────────────────────────────

static void* DUAL_AUDIO_PLUGIN_ABI sid_create(int preferred_rate_hz) {
    auto s = std::make_unique<sidplayfp_plugin_state_t>();

    s->engine = std::make_unique<sidplayfp>();
    s->rs = std::make_unique<ReSIDfpBuilder>("ReSIDfp");
    s->rs->create(s->engine->info().maxsids());
    if(!s->rs->getStatus()) {
        snprintf(g_create_error, sizeof(g_create_error),
                 "ReSIDfpBuilder::create() failed");
        return nullptr;
    }

    SidConfig cfg;
    s->rate          = preferred_rate_hz;
    cfg.frequency    = preferred_rate_hz;
    cfg.playback     = SidConfig::STEREO;
    cfg.sidEmulation = s->rs.get();
    cfg.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
    cfg.fastSampling   = false;
    if(!s->engine->config(cfg)) {
        snprintf(g_create_error, sizeof(g_create_error),
                 "sidplayfp::config() failed: %s", s->engine->error());
        return nullptr;
    }

    return s.release();
}

static void DUAL_AUDIO_PLUGIN_ABI sid_destroy(void* self) {
    delete static_cast<sidplayfp_plugin_state_t*>(self);
}

static const char* DUAL_AUDIO_PLUGIN_ABI sid_last_error(void* self) {
    auto* s = static_cast<sidplayfp_plugin_state_t*>(self);
    return s ? s->last_error : g_create_error;
}

static int DUAL_AUDIO_PLUGIN_ABI sid_can_handle(void* self, const char* path) {
    (void)self;
    const char* dot = strrchr(path, '.');
    if(!dot || !dot[1]) return 0;
    for(const char* const* ext = kExtensions; *ext; ext++) {
        const char* a = dot + 1;
        const char* b = *ext;
        while(*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
            if(ca != *b) break;
            a++; b++;
        }
        if(!*a && !*b) return 1;
    }
    return 0;
}

static int DUAL_AUDIO_PLUGIN_ABI sid_load(void* self, const char* path,
                                           dual_song_meta_t* out_meta) {
    auto* s = static_cast<sidplayfp_plugin_state_t*>(self);

    auto tune = std::make_unique<SidTune>(path);
    if(!tune->getStatus()) {
        snprintf(s->last_error, sizeof(s->last_error),
                 "SidTune load failed: %s", tune->statusString());
        return 0;
    }
    const SidTuneInfo* ti = tune->getInfo();
    if(!ti) {
        snprintf(s->last_error, sizeof(s->last_error), "SidTune: no info");
        return 0;
    }

    s->title[0] = s->author[0] = s->released[0] = '\0';
    if(ti->numberOfInfoStrings() >= 1 && ti->infoString(0) && ti->infoString(0)[0])
        strncpy(s->title, ti->infoString(0), sizeof(s->title) - 1);
    if(ti->numberOfInfoStrings() >= 2 && ti->infoString(1) && ti->infoString(1)[0])
        strncpy(s->author, ti->infoString(1), sizeof(s->author) - 1);
    if(ti->numberOfInfoStrings() >= 3 && ti->infoString(2) && ti->infoString(2)[0])
        strncpy(s->released, ti->infoString(2), sizeof(s->released) - 1);

    const int start_subsong = static_cast<int>(ti->startSong());
    const int num_subsongs  = static_cast<int>(ti->songs());

    tune->selectSong(start_subsong);
    if(!s->engine->load(tune.get())) {
        snprintf(s->last_error, sizeof(s->last_error),
                  "sidplayfp::load() failed: %s", s->engine->error());
        return 0;
    }

    s->tune = std::move(tune);
    s->current_subsong = start_subsong;
    s->num_subsongs = num_subsongs;
    s->playing = 1;

    static char line1_buf[288], line2_buf[160];
    snprintf(line1_buf, sizeof(line1_buf), "Author: %s", s->author);
    snprintf(line2_buf, sizeof(line2_buf), "Released: %s", s->released);

    out_meta->title           = s->title[0] ? s->title : nullptr;
    out_meta->line1           = s->author[0]   ? line1_buf : "";
    out_meta->line2           = s->released[0] ? line2_buf : "";
    out_meta->line3           = "";
    out_meta->duration_s      = 0.0;  // i SID suonano in loop, durata ignota
    out_meta->num_subsongs    = num_subsongs;
    out_meta->current_subsong = start_subsong - 1; // 0-based per l'ABI

    return 1;
}

// Il motore non si "mette in pausa": e' l'host che smette di chiamare
// render() finche' il device e' in pausa. Stessa scelta di sidplayfp_player.cpp.
static void DUAL_AUDIO_PLUGIN_ABI sid_pause(void* self)  { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI sid_resume(void* self) { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI sid_stop(void* self) {
    static_cast<sidplayfp_plugin_state_t*>(self)->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI sid_sample_rate(void* self) {
    return static_cast<sidplayfp_plugin_state_t*>(self)->rate;
}

static int DUAL_AUDIO_PLUGIN_ABI sid_render(void* self, int16_t* out, int frames) {
    auto* s = static_cast<sidplayfp_plugin_state_t*>(self);
    if(!s->playing) return 0;
    // API deprecata ma intenzionale (play(short*,count)), stereo interleaved.
    s->engine->play(out, frames * 2);
    return frames; // i SID non finiscono mai da soli
}

static int DUAL_AUDIO_PLUGIN_ABI sid_is_playing(void* self) {
    return static_cast<sidplayfp_plugin_state_t*>(self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI sid_is_paused(void* self) {
    (void)self;
    return 0; // lo stato "paused" vive nell'host, non nel motore
}

static void DUAL_AUDIO_PLUGIN_ABI sid_set_volume(void* self, int pct) {
    (void)self; (void)pct; // volume applicato dall'host dopo render()
}
static int DUAL_AUDIO_PLUGIN_ABI sid_get_volume(void* self) {
    (void)self;
    return 100;
}

static void DUAL_AUDIO_PLUGIN_ABI sid_set_subsong(void* self, int idx_0based,
                                                   dual_song_meta_t* out_meta) {
    auto* s = static_cast<sidplayfp_plugin_state_t*>(self);
    if(!s->tune) return;
    const int n = idx_0based + 1; // 1-based per l'API sidplayfp
    s->tune->selectSong(n);
    if(s->engine->load(s->tune.get())) {
        s->current_subsong = n;
        s->playing = 1;

        // Titolo/autore/data non cambiano fra subsong dello stesso tune SID
        // (a differenza di UADE, dove un domani potrebbero); solo l'indice
        // corrente serve un aggiornamento reale qui.
        static char line1_buf[288], line2_buf[160];
        snprintf(line1_buf, sizeof(line1_buf), "Author: %s", s->author);
        snprintf(line2_buf, sizeof(line2_buf), "Released: %s", s->released);

        out_meta->title           = s->title[0] ? s->title : nullptr;
        out_meta->line1           = s->author[0]   ? line1_buf : "";
        out_meta->line2           = s->released[0] ? line2_buf : "";
        out_meta->line3           = "";
        out_meta->duration_s      = 0.0;
        out_meta->num_subsongs    = s->num_subsongs;
        out_meta->current_subsong = idx_0based;
    }
}

static double DUAL_AUDIO_PLUGIN_ABI sid_get_position_seconds(void* self) {
    auto* s = static_cast<sidplayfp_plugin_state_t*>(self);
    return s->engine ? s->engine->timeMs() / 1000.0 : 0.0;
}

static int  DUAL_AUDIO_PLUGIN_ABI sid_can_seek(void* self) { (void)self; return 0; }
static void DUAL_AUDIO_PLUGIN_ABI sid_seek_seconds(void* self, double seconds) {
    (void)self; (void)seconds;
}

// ── Descrittore statico ───────────────────────────────────────────────────────

static const dual_audio_plugin_t kPlugin = {
    /* info */ {
        DUAL_AUDIO_PLUGIN_TYPE_GENERATOR,
        DUAL_AUDIO_PLUGIN_API_VMAJOR,
        DUAL_AUDIO_PLUGIN_API_VMINOR,
        2, 16, // libsidplayfp 2.16.x
        "sidplayfp",
        "libsidplayfp",
        "Commodore 64 SID chiptune playback (ReSIDfp band-limited emulation)",
        "libsidplayfp\n"
        "GNU General Public License v2.0-or-later\n"
        "https://github.com/libsidplayfp/libsidplayfp",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        kConfigDialog,
        0.040f, // fade_in_seconds — ~2 raster PAL, copre l'attacco del filtro SID
        nullptr, // extended_params
    },
    /* ops */ {
        sid_create, sid_destroy, sid_last_error, sid_can_handle,
        sid_load, sid_pause, sid_resume, sid_stop,
        sid_sample_rate, sid_render,
        sid_is_playing, sid_is_paused,
        sid_set_volume, sid_get_volume,
        sid_set_subsong, sid_get_position_seconds,
        sid_can_seek, sid_seek_seconds,
    },
};

extern "C" DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}

#pragma clang diagnostic pop
