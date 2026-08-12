// Adattatore: espone AdPlug (libreria stock di Homebrew, nessun fork,
// nessuna ricompilazione: LGPL non impone l'isolamento che serve a UADE/
// sidplayfp, questo modulo esiste solo per coerenza architetturale) dietro
// l'ABI dual_audio_plugin.h.
//
// Motore OPL selezionabile a runtime via ops.set_option("opl_engine", key)
// (ABI v1.3): CEmuopl (default, fmopl di MAME), CNemuopl (Nuked OPL3,
// cycle-accurate), CKemuopl (Ken Silverman's), CTemuopl (Tatsuyuki Satoh's).
// CWoodyopl (DOSBox) e' escluso: pur presente nei sorgenti di AdPlug, non
// espone una sottoclasse Copl (verificato con `nm` sul .a — solo
// OPLChipClass di basso livello) quindi non e' un drop-in come gli altri.
//
// Rate FISSO al nativo del chip OPL3 (49716 Hz), non a preferred_rate_hz:
// tutti e quattro i motori accettano un rate arbitrario ma sono piu' fedeli
// al proprio rate nativo (specialmente CNemuopl, cycle-accurate), quindi
// sample_rate() dichiara sempre 49716 e la conversione verso il device tocca
// a Dual (vedi PluginResampler in dual/src/audio_plugin_resampler.h).

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"

#include <adplug/adplug.h>
#include <adplug/emuopl.h>
#include <adplug/kemuopl.h>
#include <adplug/nemuopl.h>
#include <adplug/temuopl.h>
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

// Rate nativo del chip OPL3 — vedi il commento in testa al file.
static constexpr int kOplNativeRate = 49716;

struct adplug_plugin_state_t {
    std::unique_ptr<Copl> opl;
    std::unique_ptr<CPlayer> player;

    char engine[16] {"emu"}; // vedi adplug_set_option: "emu"/"nuked"/"ken"/"satoh"
    int playing {0};

    // Rampa anti-click SOLO per "nuked": Nuked OPL3 (cycle-accurate) produce
    // un breve rumore bianco all'attacco, confermato anche in DeaDBeeF sullo
    // stesso file (PHANDRAL/DREAMING.RAD) — quindi caratteristica nota del
    // motore stesso, non un bug nostro. info.fade_in_seconds nell'ABI e'
    // per-PLUGIN, non per-motore selezionato a runtime, quindi la rampa
    // dell'host non puo' distinguere "nuked" dagli altri tre — va applicata
    // qui, sui campioni, solo quando questo motore e' attivo.
    int warmup_left {0};
    int warmup_total {0};

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
    (void)preferred_rate_hz; // ignorato: si renderizza sempre al rate nativo, vedi sopra
    auto s = std::make_unique<adplug_plugin_state_t>();
    return s.release();
}

// Costruisce l'emulatore OPL scelto da "engine" ("emu" se sconosciuto/vuoto).
// settype() NON e' virtuale su Copl (verificato in opl.h: solo gettype() e'
// nella base, currType e' protected) — ogni motore lo dichiara per conto
// suo, quindi il fix del panning va per caso specifico, non un unique_ptr<Copl>
// generico con una chiamata sola:
//   - CEmuopl: default TYPE_DUAL_OPL2 (chip0->L, chip1->R) — un file che usa
//     solo chip0 (la maggioranza) suonerebbe sbilanciato a sinistra; ha un
//     proprio settype() pubblico, usato per forzare TYPE_OPL2. Stessa scelta
//     di adplug_player.cpp in Dual da sempre.
//   - CKemuopl: stesso default TYPE_DUAL_OPL2, ma NON espone settype() — il
//     suo update() instrada comunque chip0->L/chip1->R a prescindere da
//     currType quando costruito stereo, quindi il limite resta: un file
//     mono-chip su questo motore suonera' sbilanciato. Nessun modo di
//     correggerlo senza modificare AdPlug stesso.
//   - CTemuopl: currType resta TYPE_OPL2 di default (mai toccato dal proprio
//     costruttore) e update() duplica sempre mono->stereo — gia' corretto,
//     nessun settype necessario.
//   - CNemuopl: OPL3 vero, panning stereo nativo via i registri del chip,
//     ignora currType — gia' corretto.
static std::unique_ptr<Copl> adplug_make_opl(const char* engine, int rate) {
    if (strcmp(engine, "nuked") == 0) {
        return std::make_unique<CNemuopl>(rate);
    }
    if (strcmp(engine, "ken") == 0) {
        return std::make_unique<CKemuopl>(rate, /*bit16=*/true, /*stereo=*/true);
    }
    if (strcmp(engine, "satoh") == 0) {
        return std::make_unique<CTemuopl>(rate, /*bit16=*/true, /*stereo=*/true);
    }
    auto opl = std::make_unique<CEmuopl>(rate, /*bit16=*/true, /*stereo=*/true);
    opl->settype(Copl::TYPE_OPL2);
    return opl;
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

// Condivisa fra load() e set_subsong(): titolo/autore/tipo sono proprieta'
// del CPlayer (non cambiano cambiando subsong), ma la durata sì — va
// ricalcolata per il subsong corrente ad ogni chiamata.
static void adplug_fill_meta(adplug_plugin_state_t* s, dual_song_meta_t* out_meta) {
    const unsigned long length_ms =
        SafeSonglength(s->player.get(), static_cast<int>(s->subsong));

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
}

static int DUAL_AUDIO_PLUGIN_ABI adplug_load(void* self, const char* path,
                                              dual_song_meta_t* out_meta) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);

    auto opl = adplug_make_opl(s->engine, kOplNativeRate);

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

    // ~82 ms al rate nativo OPL3 (kOplNativeRate * 0.082 =~ 4077, arrotondato):
    // piu' lungo dei 40 ms usati per il pop del SID perche' qui il rumore
    // riportato dall'utente era piu' percepibile, non un semplice click.
    s->warmup_total = (strcmp(s->engine, "nuked") == 0) ? 4096 : 0;
    s->warmup_left  = s->warmup_total;

    strncpy(s->title, raw->gettitle().c_str(), sizeof(s->title) - 1);
    strncpy(s->author, raw->getauthor().c_str(), sizeof(s->author) - 1);
    strncpy(s->type, raw->gettype().c_str(), sizeof(s->type) - 1);

    adplug_fill_meta(s, out_meta);

    return 1;
}

static void DUAL_AUDIO_PLUGIN_ABI adplug_pause(void* self)  { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI adplug_resume(void* self) { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI adplug_stop(void* self) {
    static_cast<adplug_plugin_state_t*>(self)->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI adplug_sample_rate(void* self) {
    (void)self;
    return kOplNativeRate;
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
                ? static_cast<unsigned long>(kOplNativeRate / hz) : 1024UL;
        }
        const unsigned long n = std::min(
            static_cast<unsigned long>(remaining), samples_until_update);
        s->opl->update(out + pos, static_cast<int>(n));
        pos += static_cast<int>(n) * 2;
        remaining -= static_cast<int>(n);
        samples_until_update -= n;
    }

    const int written = frames - remaining;

    // Rampa anti-rumore per "nuked" — vedi il commento su warmup_total in
    // adplug_plugin_state_t. Lineare 0->1, applicata sui campioni GIA'
    // renderizzati sopra, prima di restituirli.
    if (s->warmup_left > 0) {
        const int n = std::min(written, s->warmup_left);
        for (int i = 0; i < n; i++) {
            const float g = 1.0f - static_cast<float>(s->warmup_left) / s->warmup_total;
            out[i * 2]     = static_cast<int16_t>(out[i * 2]     * g);
            out[i * 2 + 1] = static_cast<int16_t>(out[i * 2 + 1] * g);
            s->warmup_left--;
        }
    }

    return written;
}

static int DUAL_AUDIO_PLUGIN_ABI adplug_is_playing(void* self) {
    return static_cast<adplug_plugin_state_t*>(self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI adplug_is_paused(void* self) { (void)self; return 0; }

static void DUAL_AUDIO_PLUGIN_ABI adplug_set_volume(void* self, int pct) {
    (void)self; (void)pct;
}
static int DUAL_AUDIO_PLUGIN_ABI adplug_get_volume(void* self) { (void)self; return 100; }

// Applica SOLO "opl_engine" (l'unica chiave dichiarata in info.config_dialog
// qui sotto). Effetto al PROSSIMO load(): l'host lo garantisce per contratto
// (vedi set_option in dual_audio_plugin.h), quindi non serve ricostruire
// s->opl qui — cambiare motore a meta' canzone non e' previsto.
static void DUAL_AUDIO_PLUGIN_ABI adplug_set_option(void* self, const char* key, const char* value) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);
    if (!s || !key || !value) return;
    if (strcmp(key, "opl_engine") != 0) return;
    strncpy(s->engine, value, sizeof(s->engine) - 1);
    s->engine[sizeof(s->engine) - 1] = 0;
}

static void DUAL_AUDIO_PLUGIN_ABI adplug_set_subsong(void* self, int idx_0based,
                                                       dual_song_meta_t* out_meta) {
    auto* s = static_cast<adplug_plugin_state_t*>(self);
    if (!s->player) return;
    const unsigned int n = static_cast<unsigned int>(
        std::min(std::max(idx_0based, 0),
                 static_cast<int>(s->num_subsongs) - 1));
    s->player->rewind(static_cast<int>(n));
    s->subsong = n;
    s->playing = 1;
    adplug_fill_meta(s, out_meta);
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
        "AdLib/OPL2/OPL3 tracker and composer formats, motore OPL selezionabile",
        "AdPlug — Replayer for many OPL2/OPL3 audio file formats\n"
        "Copyright (C) 1999-2024 Simon Peter and contributors\n"
        "GNU Lesser General Public License v2.1-or-later\n"
        "https://adplug.github.io",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        // Nomi identici al menu motori OPL di DeaDBeeF (stessi 4 su 5 —
        // DOSBox escluso, vedi il commento su adplug_make_opl), cosi' un
        // utente che li conosce gia' da li' li ritrova uguali qui.
        "select|OPL Engine|opl_engine|emu|"
        "nuked:Nuked OPL3|"
        "satoh:Tatsuyuki Satoh's OPL2 emulator|"
        "ken:Ken Silverman's OPL emulator|"
        "emu:Simon Peter's OPL emulator (default)",
        0.0f,    // fade_in_seconds: usa il default di Dual
        nullptr, // extended_params
    },
    /* ops */ {
        adplug_create, adplug_destroy, adplug_last_error, adplug_can_handle,
        adplug_load, adplug_pause, adplug_resume, adplug_stop,
        adplug_sample_rate, adplug_render,
        adplug_is_playing, adplug_is_paused,
        adplug_set_volume, adplug_get_volume,
        adplug_set_option,
        adplug_set_subsong, adplug_get_position_seconds,
        adplug_can_seek, adplug_seek_seconds,
    },
};

extern "C" DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}
