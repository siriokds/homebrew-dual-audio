// Adattatore: espone libuade (fork dragnet) dietro l'ABI dual_audio_plugin.h.
// Linka direttamente contro libuade.dylib dello stesso modulo — a differenza
// di Dual, che oggi la carica con dlopen, qui non serve: adattatore e
// libreria sono compilati/distribuiti insieme da questo stesso repository.
//
// Logica di riproduzione rispecchia audio_player.cpp in Dual (Impl::CreateState,
// Impl::Render, Play/Stop/SetSubsong) — è la stessa identica sequenza di
// chiamate a uade_new_state/uade_play/uade_read/uade_stop, solo dietro la
// vtable generica invece che dentro Dual stesso.

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"
#include <uade/uade.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Stato per istanza ────────────────────────────────────────────────────────

typedef struct {
    struct uade_state* state;
    int  sample_rate;      // quello che il context sta davvero rendendo
    int  playing;
    char current_file[4096];
    char last_error[256];
} uade_plugin_state_t;

// Errore di create() quando self non esiste ancora (nessuna istanza a cui
// attaccare last_error) — un solo buffer di processo, come previsto
// dal commento sopra dual_audio_plugin_ops_t::create nell'header.
static char g_create_error[256] = "";

// ── Estensioni gestite ───────────────────────────────────────────────────────
// DEVE contenere l'intero elenco kUadeExts di audio_backend.cpp in Dual —
// FindPluginForExtension() richiede che l'estensione sia sia nel dispatch di
// Dual SIA in questa lista, quindi una voce mancante qui rende il plugin
// invisibile per quel formato anche se installato e funzionante (bug reale,
// riscontrato: *.dm2 non veniva trovato perche' l'elenco iniziale copriva
// solo un sottoinsieme). Più gli extra della fork dragnet (Face The Music,
// MMD3, MED4, DigiBooster, ProTrekkr, NoiseTrekker) dichiarati anche nel
// copyright sotto — questi non sono ancora in kUadeExts lato Dual, quindi
// oggi restano dichiarati qui ma irraggiungibili finche' non si aggiornano
// anche le regole di dispatch la' (prossimo passo, non in questa build).
static const char* const kExtensions[] = {
    // Amiga custom formats
    "fc", "fc3", "fc13", "fc4", "fc14", "dm", "dm2", "bp", "mon", "cust",
    "aon", "dz", "hipc", "hip", "jam", "mkii", "ma", "mc", "sa", "sonic",
    // SIDMon 1.0 / 2.0 (Amiga)
    "sid1", "smn", "sid2",
    // AHX / THX
    "ahx", "thx",
    // Other Amiga formats
    "ml", "fred", "dw",
    // C64 / console chiptune
    "nsf", "nsfe", "spc", "gym", "vgm", "ay", "gbs", "hes", "kss",
    // PC tracker — gestiti anche da OpenMPT: la scelta fra i due backend e'
    // decisione del loader in Dual, non di questo plugin.
    "mod", "s3m", "xm", "it",
    // Extra della fork dragnet, non ancora nel dispatch kUadeExts lato Dual
    "med", "mmd0", "mmd1", "mmd2", "mmd3", "dbm", "ptk", "ptt",
    NULL
};

static const char kConfigDialog[] =
    "property \"Resampler\" select[2] uade.resampler 1 \"none\" \"sinc\";\n";

// ── Helper: config UADE, stessa sequenza di Impl::CreateState in Dual ───────

static struct uade_state* CreateState(int preferred_rate_hz, int* out_rate) {
    struct uade_config* uc = uade_new_config();
    if (uc) {
        char rate_str[16];
        snprintf(rate_str, sizeof(rate_str), "%d", preferred_rate_hz);
        uade_config_set_option(uc, UC_FREQUENCY, rate_str);
        // Ricampionatore di qualita', stessa scelta motivata in
        // audio_player.cpp: "default"/"sinc" sono i valori accettati.
        uade_config_set_option(uc, UC_RESAMPLER, "sinc");
        struct uade_state* state = uade_new_state(uc);
        free(uc); // uade_new_state copia la config
        if (state) {
            *out_rate = preferred_rate_hz;
            return state;
        }
    }
    // Fallback: context di default, 44100 Hz fisso.
    *out_rate = 44100;
    return uade_new_state(NULL);
}

// ── ops ──────────────────────────────────────────────────────────────────────

static void* DUAL_AUDIO_PLUGIN_ABI uade_plugin_create(int preferred_rate_hz) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)calloc(1, sizeof(uade_plugin_state_t));
    if (!s) {
        snprintf(g_create_error, sizeof(g_create_error), "out of memory");
        return NULL;
    }

    s->state = CreateState(preferred_rate_hz, &s->sample_rate);
    if (!s->state) {
        snprintf(g_create_error, sizeof(g_create_error),
                 "uade_new_state() failed (corrupt install or missing uaerc?)");
        free(s);
        return NULL;
    }
    return s;
}

static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_destroy(void* self) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)self;
    if (!s) return;
    if (s->playing) uade_stop(s->state);
    if (s->state) uade_cleanup_state(s->state);
    free(s);
}

static const char* DUAL_AUDIO_PLUGIN_ABI uade_plugin_last_error(void* self) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)self;
    return s ? s->last_error : g_create_error;
}

static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_can_handle(void* self, const char* path) {
    (void)self;
    const char* dot = strrchr(path, '.');
    if (!dot || !dot[1]) return 0;
    for (const char* const* ext = kExtensions; *ext; ext++) {
        // confronto case-insensitive senza dipendenze locale-specifiche
        const char* a = dot + 1;
        const char* b = *ext;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*a && !*b) return 1;
    }
    return 0;
}

// Condivisa fra load() e set_subsong(): entrambi devono restituire metadati
// aggiornati (set_subsong() cambia titolo/formato quando i subsong sono
// moduli diversi, non solo un offset nello stesso file).
static void uade_plugin_fill_meta(uade_plugin_state_t* s, const char* path,
                                   dual_song_meta_t* out_meta) {
    const struct uade_song_info* si = uade_get_song_info(s->state);

    static char title_buf[256], line1_buf[256], line2_buf[256], line3_buf[128];
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (si && si->modulename[0]) {
        strncpy(title_buf, si->modulename, sizeof(title_buf) - 1);
        title_buf[sizeof(title_buf) - 1] = '\0';
    } else {
        strncpy(title_buf, base, sizeof(title_buf) - 1);
        title_buf[sizeof(title_buf) - 1] = '\0';
    }

    snprintf(line1_buf, sizeof(line1_buf), "Format: %s",
             si ? si->formatname : "unknown");
    snprintf(line2_buf, sizeof(line2_buf), "Player: %s",
             si ? si->playername : "unknown");
    snprintf(line3_buf, sizeof(line3_buf), "%llu bytes",
             si ? (unsigned long long)si->modulebytes : 0ULL);

    out_meta->title            = title_buf;
    out_meta->line1            = line1_buf;
    out_meta->line2            = line2_buf;
    out_meta->line3            = line3_buf;
    out_meta->duration_s       = si ? si->duration : 0.0;
    out_meta->num_subsongs     = si ? (si->subsongs.max - si->subsongs.min + 1) : 1;
    out_meta->current_subsong  = si ? (si->subsongs.cur - si->subsongs.min) : 0;
}

static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_load(void* self, const char* path,
                                                   dual_song_meta_t* out_meta) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)self;

    if (s->playing) {
        uade_stop(s->state);
        s->playing = 0;
    }

    strncpy(s->current_file, path, sizeof(s->current_file) - 1);
    s->current_file[sizeof(s->current_file) - 1] = '\0';

    int r = uade_play(path, -1, s->state);
    if (r != 1) {
        snprintf(s->last_error, sizeof(s->last_error),
                 "uade_play() rejected: %s", path);
        return 0;
    }
    s->playing = 1;

    uade_plugin_fill_meta(s, path, out_meta);
    return 1;
}

static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_pause(void* self) {
    // Nessuna chiamata UADE: stesso comportamento di AudioPlayer::Pause() in
    // Dual oggi — il device smette di chiamare render(), UADE non lo sa nemmeno.
    (void)self;
}
static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_resume(void* self) { (void)self; }

static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_stop(void* self) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)self;
    if (!s->playing) return;
    uade_stop(s->state);
    s->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_sample_rate(void* self) {
    return ((uade_plugin_state_t*)self)->sample_rate;
}

static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_render(void* self, int16_t* out, int frames) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)self;
    if (!s->playing) return 0;

    const ssize_t bytes_wanted = (ssize_t)frames * 4; // stereo 16-bit
    const ssize_t n = uade_read(out, (size_t)bytes_wanted, s->state);
    if (n <= 0) {
        s->playing = 0;
        return n < 0 ? -1 : 0;
    }
    return (int)(n / 4);
}

static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_is_playing(void* self) {
    return ((uade_plugin_state_t*)self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_is_paused(void* self) {
    (void)self;
    return 0; // vedi nota su pause(): lo stato "paused" vive nell'host, non qui
}

static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_set_volume(void* self, int pct) {
    (void)self; (void)pct; // volume applicato dall'host dopo render(), come oggi in Dual
}
static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_get_volume(void* self) {
    (void)self;
    return 100;
}

static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_set_subsong(void* self, int idx_0based,
                                                            dual_song_meta_t* out_meta) {
    uade_plugin_state_t* s = (uade_plugin_state_t*)self;
    if (s->playing) uade_stop(s->state);
    int r = uade_play(s->current_file, idx_0based, s->state);
    s->playing = (r == 1);
    if (s->playing) uade_plugin_fill_meta(s, s->current_file, out_meta);
}

static double DUAL_AUDIO_PLUGIN_ABI uade_plugin_get_position_seconds(void* self) {
    (void)self;
    return 0.0; // Dual calcola la posizione dai byte consumati lato host, non qui
}

static int DUAL_AUDIO_PLUGIN_ABI uade_plugin_can_seek(void* self) {
    (void)self;
    return 0; // sintesi, non stream: nessun seek, come IAudioBackend::CanSeek() oggi
}
static void DUAL_AUDIO_PLUGIN_ABI uade_plugin_seek_seconds(void* self, double seconds) {
    (void)self; (void)seconds;
}

// ── Descrittore statico ───────────────────────────────────────────────────────

static const dual_audio_plugin_t kPlugin = {
    /* info */ {
        DUAL_AUDIO_PLUGIN_TYPE_GENERATOR,
        DUAL_AUDIO_PLUGIN_API_VMAJOR,
        DUAL_AUDIO_PLUGIN_API_VMINOR,
        3, 5, // 3.05-dragnet
        "uade",
        "UADE (dragnet fork)",
        "Amiga custom formats + C64/console chiptune + PC tracker files, "
        "with extra support for Face The Music, OctaMED MMD3, MED4, "
        "DigiBooster, ProTrekkr, NoiseTrekker over stock UADE.",
        "UADE (Unix Amiga Delitracker Emulator), fork mvtiaine/uade (dragnet)\n"
        "GNU General Public License v2.0 — see modules/uade/src/COPYING\n"
        "https://gitlab.com/mvtiaine/uade",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        kConfigDialog,
        0.0f, // fade_in_seconds: usa il default di Dual
        NULL, // extended_params
    },
    /* ops */ {
        uade_plugin_create, uade_plugin_destroy, uade_plugin_last_error,
        uade_plugin_can_handle,
        uade_plugin_load, uade_plugin_pause, uade_plugin_resume, uade_plugin_stop,
        uade_plugin_sample_rate, uade_plugin_render,
        uade_plugin_is_playing, uade_plugin_is_paused,
        uade_plugin_set_volume, uade_plugin_get_volume,
        uade_plugin_set_subsong, uade_plugin_get_position_seconds,
        uade_plugin_can_seek, uade_plugin_seek_seconds,
    },
};

DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}
