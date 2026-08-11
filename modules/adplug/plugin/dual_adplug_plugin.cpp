// Adattatore: espone AdPlug (libreria stock di Homebrew, nessun fork,
// nessuna ricompilazione: LGPL non impone l'isolamento che serve a UADE/
// sidplayfp, questo modulo esiste solo per coerenza architetturale) dietro
// l'ABI dual_audio_plugin.h.
//
// Motore fisso CEmuopl, stesso di adplug_player.cpp in Dual oggi — la
// scelta del motore OPL (Nuked/Ken Silverman's/Satoh's/DOSBox, tutti gia'
// presenti dentro AdPlug stesso, verificato) resta un passo futuro: serve
// prima un parser della DSL config_dialog lato Dual, che oggi non esiste.

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"

#include <adplug/adplug.h>
#include <adplug/emuopl.h>
#include <adplug/player.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <setjmp.h>

// ── SafeSonglength ────────────────────────────────────────────────────────
// Stessa protezione di adplug_player.cpp in Dual: alcuni CPlayer di AdPlug
// (es. Ca2mv2Player) hanno bug che fanno SIGSEGV su file specifici durante
// songlength(). Un fence sigsetjmp/siglongjmp isola il crash invece di
// tirarsi dietro tutto il processo host.
static sigjmp_buf s_songlength_jmp;
static volatile sig_atomic_t s_in_songlength = 0;

static void SonglengthFaultHandler(int) {
    if (s_in_songlength) siglongjmp(s_songlength_jmp, 1);
}

static unsigned long SafeSonglength(CPlayer* player, int subsong) {
    struct sigaction sa_new {}, sa_old {};
    sa_new.sa_handler = SonglengthFaultHandler;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;
    sigaction(SIGSEGV, &sa_new, &sa_old);

    unsigned long result = 0;
    s_in_songlength = 1;
    if (sigsetjmp(s_songlength_jmp, /*savemask=*/1) == 0)
        result = player->songlength(subsong);
    s_in_songlength = 0;

    sigaction(SIGSEGV, &sa_old, nullptr);
    return result;
}

// ── Stato per istanza ────────────────────────────────────────────────────────

struct adplug_plugin_state_t {
    std::unique_ptr<CEmuopl> opl;
    std::unique_ptr<CPlayer> player;

    int rate {0};
    int playing {0};

    unsigned int subsong {0};
    unsigned int num_subsongs {1};

    char title[256] {};
    char author[256] {};
    char type[128] {};
    char last_error[256] {};
};

static char g_create_error[256] = "";

static const char* const kExtensions[] = {
    // Id Music Format
    "imf", "wlf",
    // Classic OPL drivers
    "cmf", "d00", "d01", "dro", "hsc", "rad", "rix", "rol",
    // Tracker / composer formats
    "a2m", "ad", "adl", "agd", "amd", "bam", "bmf", "cff", "cym",
    "dtm", "edl", "ems", "fmc", "fmf", "fmk", "fms", "fmt", "ftf",
    "gmd", "gms", "got", "ims", "jbm", "ksm", "lds", "m2m", "mdi",
    "mfp", "mkj", "msc", "mtk", "mtr", "mus", "mzk", "nka",
    "not", "pis", "plx", "rmf", "sa2", "sdb", "sig", "sng", "sop",
    "src", "vib", "wm", "xad", "xsm",
    nullptr
};

// ── ops ──────────────────────────────────────────────────────────────────────

static void* DUAL_AUDIO_PLUGIN_ABI adplug_create(int preferred_rate_hz) {
    auto s = std::make_unique<adplug_plugin_state_t>();
    s->rate = preferred_rate_hz;
    return s.release();
}

static void DUAL_AUDIO_PLUGIN_ABI adplug_destroy(void* self) {
    delete static_cast<adplug_plugin_state_t*>(self);
}

static const char* DUAL_AUDIO_PLUGIN_ABI adplug_last_error(void* self) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);
    return s ? s->last_error : g_create_error;
}

static int DUAL_AUDIO_PLUGIN_ABI adplug_can_handle(void* self, const char* path) {
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

static int DUAL_AUDIO_PLUGIN_ABI adplug_load(void* self, const char* path,
                                              dual_song_meta_t* out_meta) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);

    // Forza TYPE_OPL2 (chip singolo): il default TYPE_DUAL_OPL2 mappa
    // chip0->L e chip1->R, quindi un file che usa solo chip0 (la
    // maggioranza) suonerebbe sbilanciato a sinistra — stessa scelta di
    // adplug_player.cpp oggi.
    auto opl = std::make_unique<CEmuopl>(s->rate, /*bit16=*/true, /*stereo=*/true);
    opl->settype(Copl::TYPE_OPL2);

    CPlayer* raw = CAdPlug::factory(path, opl.get());
    if (!raw) {
        snprintf(s->last_error, sizeof(s->last_error),
                  "CAdPlug::factory() rejected: %s", path);
        return 0;
    }

    s->opl.reset(opl.release());
    s->player.reset(raw);
    s->subsong = raw->getsubsong();
    s->num_subsongs = raw->getsubsongs();
    s->playing = 1;

    strncpy(s->title, raw->gettitle().c_str(), sizeof(s->title) - 1);
    strncpy(s->author, raw->getauthor().c_str(), sizeof(s->author) - 1);
    strncpy(s->type, raw->gettype().c_str(), sizeof(s->type) - 1);
    const unsigned long length_ms = SafeSonglength(raw, static_cast<int>(s->subsong));

    static char line1_buf[160], line2_buf[288];
    snprintf(line1_buf, sizeof(line1_buf), "Type: %s", s->type);
    snprintf(line2_buf, sizeof(line2_buf), "Author: %s", s->author);

    out_meta->title = s->title[0] ? s->title : nullptr;
    out_meta->line1 = s->type[0]   ? line1_buf : "";
    out_meta->line2 = s->author[0] ? line2_buf : "";
    out_meta->line3 = "";
    out_meta->duration_s = length_ms > 0 ? length_ms / 1000.0 : 0.0;
    out_meta->num_subsongs = static_cast<int>(s->num_subsongs);
    out_meta->current_subsong = static_cast<int>(s->subsong);

    return 1;
}

static void DUAL_AUDIO_PLUGIN_ABI adplug_pause(void* self)  { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI adplug_resume(void* self) { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI adplug_stop(void* self) {
    static_cast<adplug_plugin_state_t*>(self)->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI adplug_sample_rate(void* self) {
    return static_cast<adplug_plugin_state_t*>(self)->rate;
}

// AdPlug renderizza a "tick" (ritmo del player, non del device): un tick puo'
// finire a meta' di un buffer richiesto. Si accumula in blocchi di n frame
// finche' "frames" e' pieno o il player segnala la fine — stessa logica di
// Impl::Render in adplug_player.cpp, qui senza il gancio di posizione (lo
// calcola gia' l'host dai byte consumati, vedi audio_plugin_backend.cpp).
static int DUAL_AUDIO_PLUGIN_ABI adplug_render(void* self, int16_t* out, int frames) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);
    if (!s->playing) return 0;

    static thread_local unsigned long samples_until_update = 0;
    int pos = 0;
    int remaining = frames;

    while (remaining > 0) {
        if (samples_until_update == 0) {
            if (!s->player->update()) {
                s->playing = 0;
                break;
            }
            const float hz = s->player->getrefresh();
            samples_until_update = (hz > 0.0f)
                ? static_cast<unsigned long>(s->rate / hz) : 1024UL;
        }
        const unsigned long n = std::min(
            static_cast<unsigned long>(remaining), samples_until_update);
        s->opl->update(out + pos, static_cast<int>(n));
        pos += static_cast<int>(n) * 2;
        remaining -= static_cast<int>(n);
        samples_until_update -= n;
    }

    return frames - remaining;
}

static int DUAL_AUDIO_PLUGIN_ABI adplug_is_playing(void* self) {
    return static_cast<adplug_plugin_state_t*>(self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI adplug_is_paused(void* self) { (void)self; return 0; }

static void DUAL_AUDIO_PLUGIN_ABI adplug_set_volume(void* self, int pct) {
    (void)self; (void)pct;
}
static int DUAL_AUDIO_PLUGIN_ABI adplug_get_volume(void* self) { (void)self; return 100; }

static void DUAL_AUDIO_PLUGIN_ABI adplug_set_subsong(void* self, int idx_0based) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);
    if (!s->player) return;
    const unsigned int n = static_cast<unsigned int>(
        std::min(std::max(idx_0based, 0),
                 static_cast<int>(s->num_subsongs) - 1));
    s->player->rewind(static_cast<int>(n));
    s->subsong = n;
    s->playing = 1;
}

static double DUAL_AUDIO_PLUGIN_ABI adplug_get_position_seconds(void* self) {
    (void)self;
    return 0.0; // l'host calcola la posizione dai byte consumati
}

static int  DUAL_AUDIO_PLUGIN_ABI adplug_can_seek(void* self) { (void)self; return 0; }
static void DUAL_AUDIO_PLUGIN_ABI adplug_seek_seconds(void* self, double seconds) {
    (void)self; (void)seconds;
}

// ── Descrittore statico ───────────────────────────────────────────────────────

static const dual_audio_plugin_t kPlugin = {
    /* info */ {
        DUAL_AUDIO_PLUGIN_TYPE_GENERATOR,
        DUAL_AUDIO_PLUGIN_API_VMAJOR,
        DUAL_AUDIO_PLUGIN_API_VMINOR,
        2, 4, // AdPlug 2.4 (stock Homebrew)
        "adplug",
        "AdPlug",
        "AdLib/OPL2/OPL3 tracker and composer formats (CEmuopl engine)",
        "AdPlug — Replayer for many OPL2/OPL3 audio file formats\n"
        "Copyright (C) 1999-2024 Simon Peter and contributors\n"
        "GNU Lesser General Public License v2.1-or-later\n"
        "https://adplug.github.io",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        nullptr, // config_dialog: scelta motore rimandata (serve il parser DSL lato Dual)
        0.0f,    // fade_in_seconds: usa il default di Dual
        nullptr, // extended_params
    },
    /* ops */ {
        adplug_create, adplug_destroy, adplug_last_error, adplug_can_handle,
        adplug_load, adplug_pause, adplug_resume, adplug_stop,
        adplug_sample_rate, adplug_render,
        adplug_is_playing, adplug_is_paused,
        adplug_set_volume, adplug_get_volume,
        adplug_set_subsong, adplug_get_position_seconds,
        adplug_can_seek, adplug_seek_seconds,
    },
};

extern "C" DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}
