// Adattatore: espone libopenmpt (libreria stock di Homebrew, nessun fork,
// nessuna ricompilazione: BSD-3-Clause non impone alcun isolamento, questo
// modulo esiste solo per coerenza architetturale, come dual-adplug) dietro
// l'ABI dual_audio_plugin.h.
//
// A differenza di UADE/sidplayfp/AdPlug, openmpt::module legge a QUALSIASI
// frequenza passata ad ogni chiamata di read_interleaved_stereo() — non
// serve fissare un rate alla creazione, il parametro preferred_rate_hz di
// create() e' semplicemente quello che passiamo ad ogni render().

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"

#include <libopenmpt/libopenmpt.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

// ── Stato per istanza ────────────────────────────────────────────────────────

struct openmpt_plugin_state_t {
    std::unique_ptr<openmpt::module> module;

    int rate {0};
    int playing {0};

    int num_subsongs {1};
    int current_subsong {0};

    char title[256] {};
    char last_error[256] {};
};

static char g_create_error[256] = "";

static const char* const kExtensions[] = {
    "mod", "s3m", "xm", "it", "669", "amf", "ams", "dbm", "far",
    "mdl", "med", "mtm", "okta", "ptm", "stm", "ult", "umx", "wow",
    nullptr
};

// ── ops ──────────────────────────────────────────────────────────────────────

static void* DUAL_AUDIO_PLUGIN_ABI mpt_create(int preferred_rate_hz) {
    auto s = std::make_unique<openmpt_plugin_state_t>();
    s->rate = preferred_rate_hz;
    return s.release();
}

static void DUAL_AUDIO_PLUGIN_ABI mpt_destroy(void* self) {
    delete static_cast<openmpt_plugin_state_t*>(self);
}

static const char* DUAL_AUDIO_PLUGIN_ABI mpt_last_error(void* self) {
    auto* s = static_cast<openmpt_plugin_state_t*>(self);
    return s ? s->last_error : g_create_error;
}

static int DUAL_AUDIO_PLUGIN_ABI mpt_can_handle(void* self, const char* path) {
    (void)self;
    const char* dot = strrchr(path, '.');
    if (!dot || !dot[1]) return 0;
    for (const char* const* ext = kExtensions; *ext; ext++) {
        const char* a = dot + 1;
        const char* b = *ext;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
            if (ca != *b) break;
            a++; b++;
        }
        if (!*a && !*b) return 1;
    }
    return 0;
}

// Condivisa fra load() e set_subsong(): il titolo/formato di un modulo
// OpenMPT non cambia fra subsong (stessa considerazione fatta per AdPlug/
// sidplayfp — i tre campi restano quelli dell'intero file), ma durata e
// indice sì.
static void mpt_fill_meta(openmpt_plugin_state_t* s, dual_song_meta_t* out_meta) {
    static char line1_buf[256], line2_buf[256], line3_buf[64];

    const std::string type_long = s->module->get_metadata("type_long");
    snprintf(line1_buf, sizeof(line1_buf), "Format: %s", type_long.c_str());

    const std::string tracker = s->module->get_metadata("tracker");
    const std::string artist  = s->module->get_metadata("artist");
    if (!tracker.empty())
        snprintf(line2_buf, sizeof(line2_buf), "Tracker: %s", tracker.c_str());
    else if (!artist.empty())
        snprintf(line2_buf, sizeof(line2_buf), "Artist: %s", artist.c_str());
    else
        line2_buf[0] = '\0';

    const double dur = s->module->get_duration_seconds();
    line3_buf[0] = '\0';
    if (dur > 0.0) {
        const int total = static_cast<int>(dur);
        snprintf(line3_buf, sizeof(line3_buf), "%d:%02d", total / 60, total % 60);
    }
    if (s->num_subsongs > 1) {
        char sub[32];
        snprintf(sub, sizeof(sub), "%s%d subsongs",
                 line3_buf[0] ? " \xc2\xb7 " : "", s->num_subsongs);
        strncat(line3_buf, sub, sizeof(line3_buf) - strlen(line3_buf) - 1);
    }

    out_meta->title           = s->title[0] ? s->title : nullptr;
    out_meta->line1           = type_long.empty() ? "" : line1_buf;
    out_meta->line2           = line2_buf;
    out_meta->line3           = line3_buf;
    out_meta->duration_s      = dur;
    out_meta->num_subsongs    = s->num_subsongs;
    out_meta->current_subsong = s->current_subsong;
}

static int DUAL_AUDIO_PLUGIN_ABI mpt_load(void* self, const char* path,
                                           dual_song_meta_t* out_meta) {
    auto* s = static_cast<openmpt_plugin_state_t*>(self);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        snprintf(s->last_error, sizeof(s->last_error), "cannot open: %s", path);
        return 0;
    }

    std::unique_ptr<openmpt::module> mod;
    try {
        mod = std::make_unique<openmpt::module>(file);
    } catch (const std::exception& e) {
        snprintf(s->last_error, sizeof(s->last_error),
                 "openmpt::module() rejected %s: %s", path, e.what());
        return 0;
    }

    // Chiede di suonare una volta sola, senza loop — ignorato in silenzio se
    // la ctl non e' supportata dalla versione installata.
    try { mod->ctl_set_integer("play.loopcount", 0); } catch (...) {}

    // Interpolazione al massimo offerto da libopenmpt (8 = windowed sinc a
    // 8 tap), stessa scelta motivata in openmpt_backend.cpp: i campioni sono
    // spesso a 8 bit e vanno ricampionati ad ogni nota, quindi la qualita' si
    // gioca sull'interpolatore, non sulla profondita' del buffer di uscita.
    try {
        mod->set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH, 8);
    } catch (...) {}
    // VOLUMERAMPING lasciato al default: OpenMPT fa le proprie rampe di
    // volume, azzerarle produce click (documentato nella sua stessa API).

    s->module = std::move(mod);
    s->num_subsongs = std::max(1, s->module->get_num_subsongs());
    s->current_subsong = std::max(0, s->module->get_selected_subsong());
    s->playing = 1;

    const std::string title = s->module->get_metadata("title");
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(s->title, title.empty() ? base : title.c_str(), sizeof(s->title) - 1);

    mpt_fill_meta(s, out_meta);
    return 1;
}

static void DUAL_AUDIO_PLUGIN_ABI mpt_pause(void* self)  { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI mpt_resume(void* self) { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI mpt_stop(void* self) {
    static_cast<openmpt_plugin_state_t*>(self)->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI mpt_sample_rate(void* self) {
    return static_cast<openmpt_plugin_state_t*>(self)->rate;
}

static int DUAL_AUDIO_PLUGIN_ABI mpt_render(void* self, int16_t* out, int frames) {
    auto* s = static_cast<openmpt_plugin_state_t*>(self);
    if (!s->playing || !s->module) return 0;
    const std::size_t produced =
        s->module->read_interleaved_stereo(s->rate, static_cast<std::size_t>(frames), out);
    if (produced == 0) s->playing = 0;
    return static_cast<int>(produced);
}

static int DUAL_AUDIO_PLUGIN_ABI mpt_is_playing(void* self) {
    return static_cast<openmpt_plugin_state_t*>(self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI mpt_is_paused(void* self) { (void)self; return 0; }

static void DUAL_AUDIO_PLUGIN_ABI mpt_set_volume(void* self, int pct) {
    (void)self; (void)pct;
}
static int DUAL_AUDIO_PLUGIN_ABI mpt_get_volume(void* self) { (void)self; return 100; }

static void DUAL_AUDIO_PLUGIN_ABI mpt_set_subsong(void* self, int idx_0based,
                                                   dual_song_meta_t* out_meta) {
    auto* s = static_cast<openmpt_plugin_state_t*>(self);
    if (!s->module) return;
    idx_0based = std::clamp(idx_0based, 0, s->num_subsongs - 1);
    s->module->select_subsong(idx_0based);
    s->current_subsong = idx_0based;
    s->playing = 1;
    mpt_fill_meta(s, out_meta);
}

static double DUAL_AUDIO_PLUGIN_ABI mpt_get_position_seconds(void* self) {
    auto* s = static_cast<openmpt_plugin_state_t*>(self);
    return s->module ? s->module->get_position_seconds() : 0.0;
}

static int  DUAL_AUDIO_PLUGIN_ABI mpt_can_seek(void* self) { (void)self; return 0; }
static void DUAL_AUDIO_PLUGIN_ABI mpt_seek_seconds(void* self, double seconds) {
    (void)self; (void)seconds;
}

// ── Descrittore statico ───────────────────────────────────────────────────────

static const dual_audio_plugin_t kPlugin = {
    /* info */ {
        DUAL_AUDIO_PLUGIN_TYPE_GENERATOR,
        DUAL_AUDIO_PLUGIN_API_VMAJOR,
        DUAL_AUDIO_PLUGIN_API_VMINOR,
        0, 8, // libopenmpt 0.8.x (stock Homebrew)
        "openmpt",
        "libopenmpt",
        "PC tracker formats: MOD, S3M, XM, IT, and many more",
        "libopenmpt\n"
        "Copyright (C) 2004-2024 OpenMPT contributors\n"
        "Copyright (C) 1997-2003 Olivier Lapicque\n"
        "BSD-3-Clause\n"
        "https://lib.openmpt.org",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        nullptr, // config_dialog
        0.0f,    // fade_in_seconds: usa il default di Dual
        nullptr, // extended_params
    },
    /* ops */ {
        mpt_create, mpt_destroy, mpt_last_error, mpt_can_handle,
        mpt_load, mpt_pause, mpt_resume, mpt_stop,
        mpt_sample_rate, mpt_render,
        mpt_is_playing, mpt_is_paused,
        mpt_set_volume, mpt_get_volume,
        mpt_set_subsong, mpt_get_position_seconds,
        mpt_can_seek, mpt_seek_seconds,
    },
};

extern "C" DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}
