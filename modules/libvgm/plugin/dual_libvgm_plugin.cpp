// Adattatore: espone libvgm (ValleyBell/libvgm, vendorizzato in
// modules/libvgm/src/) dietro l'ABI dual_audio_plugin.h.
//
// libvgm non e' una libreria decoder-singola come libsidplayfp: e' un
// player generico (PlayerA) che sceglie fra piu' motori di formato
// registrati (VGMPlayer, S98Player, DROPlayer, GYMPlayer...) in base al
// contenuto del file, ognuno dei quali smista i comandi verso i core dei
// singoli chip (emu/cores/*.c, ~30 chip diversi). Qui registriamo solo
// VGMPlayer: le estensioni dichiarate sono .vgm/.vgz, non gli altri
// formati che PlayerA saprebbe comunque riconoscere.
//
// .vgz (VGM gzippato) non richiede un loader separato: VGMPlayer decomprime
// da solo (libvgm linka zlib per questo).

#define DUAL_AUDIO_PLUGIN_BUILDING
#include "dual_audio_plugin.h"

#include "../src/player/playera.hpp"
#include "../src/player/vgmplayer.hpp"
#include "../src/utils/FileLoader.h"
#include "../src/utils/DataLoader.h"

#include <cstdio>
#include <cstring>
#include <memory>

// ── PlayerA/VGMPlayer condivisi a livello di processo ────────────────────────
//
// libvgm non e' pensata per costruire e distruggere PlayerA/VGMPlayer piu'
// volte nella vita di un processo: il suo stesso tool CLI (player/player.cpp
// a monte) li tiene per tutta la durata del programma, mai smontati a meta'.
// Costruirli per-istanza in vgm_create()/vgm_destroy() produce un
// use-after-free reale in ~PlayerA()/~VGMPlayer() qualunque sia l'ordine di
// dichiarazione dei due unique_ptr (verificato con lldb: EXC_BAD_ACCESS
// dentro UnregisterAllPlayers() e poi, corretto quello, di nuovo dentro
// ~VGMPlayer() — sintomo di una libreria che non si aspetta questo ciclo
// di vita). Soluzione: singleton "Meyer's" a livello di funzione — costruito
// una volta, mai distrutto (leak deliberato, innocuo: vive quanto il
// processo). Solo un file alla volta viene comunque caricato/suonato, che e'
// esattamente il caso d'uso di Dual (un PluginBackend attivo per volta).
struct VgmEngine {
    VGMPlayer vgmPlayer;
    PlayerA   playerA;
    VgmEngine() { playerA.RegisterPlayerEngine(&vgmPlayer); }
};

static VgmEngine& SharedEngine(int rate)
{
    // `static VgmEngine engine;` (per valore) NON basta: una static locale
    // con distruttore non banale viene comunque distrutta a exit() tramite
    // __cxa_atexit — il crash si sposta semplicemente dalla fine di
    // vgm_destroy() alla chiusura del processo (verificato: SIGABRT dopo
    // "OK" nel test standalone). Serve un leak vero, deliberato: allocare
    // con `new` e non liberare mai, cosi' nessun distruttore di VgmEngine/
    // PlayerA/VGMPlayer gira mai, in nessun momento della vita del processo.
    static VgmEngine* engine = new VgmEngine();
    static int configured_rate = -1;
    if(configured_rate != rate) {
        // Il quarto parametro (smplBufferLen) NON e' opzionale nonostante il
        // nome suggerisca un hint: e' la vera capacita' (in campioni) del
        // buffer di staging interno di PlayerA (_smplBuf.resize(...) in
        // playera.cpp). Con 0, Render() clampa smplCount a 0 e non produce
        // mai un solo campione, silenziosamente (bug reale, trovato con un
        // .vgm sintetico: bytes_done restava 0 ad ogni chiamata pur con
        // GetState() == PLAYSTATE_PLAY). 8192 copre ampiamente i chunk da
        // 2048 frame che Dual chiede per ciclo di render.
        engine->playerA.SetOutputSettings(static_cast<UINT32>(rate), 2, 16, 8192);
        configured_rate = rate;
    }
    return *engine;
}

// ── Stato per istanza ────────────────────────────────────────────────────────
// Leggero apposta: non possiede PlayerA/VGMPlayer (vedi SharedEngine sopra),
// solo il file correntemente caricato e i metadati derivati.

struct vgm_plugin_state_t {
    DATA_LOADER* loader {nullptr};

    int  rate {0};
    int  playing {0};

    char title[256] {};
    char game[256] {};
    char system_name[128] {};
    char last_error[256] {};
};

static char g_create_error[256] = "";

static const char* const kExtensions[] = { "vgm", "vgz", nullptr };

// Nessun parametro configurabile oggi: PlayerA sceglie automaticamente i
// core dei chip in base a cosa dichiara l'header VGM, nessuna scelta
// dell'utente da esporre (a differenza di AdPlug, dove il motore OPL e'
// scelta reale — qui il "motore" e' il chip stesso, non negoziabile).
static const char* const kConfigDialog = nullptr;

// ── Helper: legge un tag GD3 da PlayerBase::GetTags() ───────────────────────
// GetTags() ritorna un array flat chiave/valore terminato da NULL:
// {"TITLE", "Song Title", "GAME", "Game Name", ..., NULL}. Chiavi standard
// GD3: TITLE(-JPN), GAME(-JPN), SYSTEM(-JPN), ARTIST(-JPN), DATE,
// ENCODED_BY, COMMENT.
static const char* find_tag(const char* const* tags, const char* key)
{
    if(!tags) return nullptr;
    for(const char* const* t = tags; t[0] && t[1]; t += 2)
        if(std::strcmp(t[0], key) == 0 && t[1][0])
            return t[1];
    return nullptr;
}

// ── ops ──────────────────────────────────────────────────────────────────────

static void* DUAL_AUDIO_PLUGIN_ABI vgm_create(int preferred_rate_hz) {
    auto s = std::make_unique<vgm_plugin_state_t>();
    s->rate = preferred_rate_hz;
    SharedEngine(preferred_rate_hz); // forza la costruzione/configurazione ora
    return s.release();
}

static void DUAL_AUDIO_PLUGIN_ABI vgm_destroy(void* self) {
    auto* s = static_cast<vgm_plugin_state_t*>(self);
    if(s->loader) {
        SharedEngine(s->rate).playerA.UnloadFile();
        DataLoader_Deinit(s->loader);
    }
    delete s;
}

static const char* DUAL_AUDIO_PLUGIN_ABI vgm_last_error(void* self) {
    auto* s = static_cast<vgm_plugin_state_t*>(self);
    return s ? s->last_error : g_create_error;
}

static int DUAL_AUDIO_PLUGIN_ABI vgm_can_handle(void* self, const char* path) {
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

static int DUAL_AUDIO_PLUGIN_ABI vgm_load(void* self, const char* path,
                                           dual_song_meta_t* out_meta) {
    auto* s = static_cast<vgm_plugin_state_t*>(self);
    VgmEngine& eng = SharedEngine(s->rate);

    if(s->loader) {
        eng.playerA.UnloadFile();
        DataLoader_Deinit(s->loader);
        s->loader = nullptr;
    }

    DATA_LOADER* dl = FileLoader_Init(path);
    if(!dl) {
        snprintf(s->last_error, sizeof(s->last_error), "FileLoader_Init failed: %s", path);
        return 0;
    }
    if(DataLoader_Load(dl) != 0) {
        snprintf(s->last_error, sizeof(s->last_error), "DataLoader_Load failed: %s", path);
        DataLoader_Deinit(dl);
        return 0;
    }

    if(eng.playerA.LoadFile(dl) != 0) {
        snprintf(s->last_error, sizeof(s->last_error), "PlayerA::LoadFile failed (not a VGM file?): %s", path);
        DataLoader_Deinit(dl);
        return 0;
    }
    s->loader = dl;

    if(eng.playerA.Start() != 0) {
        snprintf(s->last_error, sizeof(s->last_error), "PlayerA::Start failed: %s", path);
        eng.playerA.UnloadFile();
        DataLoader_Deinit(s->loader);
        s->loader = nullptr;
        return 0;
    }
    s->playing = 1;

    s->title[0] = s->game[0] = s->system_name[0] = '\0';
    const char* const* tags = eng.vgmPlayer.GetTags();
    if(const char* t = find_tag(tags, "TITLE"))  strncpy(s->title,       t, sizeof(s->title) - 1);
    if(const char* t = find_tag(tags, "GAME"))   strncpy(s->game,        t, sizeof(s->game) - 1);
    if(const char* t = find_tag(tags, "SYSTEM")) strncpy(s->system_name, t, sizeof(s->system_name) - 1);

    static char line1_buf[288], line2_buf[160];
    snprintf(line1_buf, sizeof(line1_buf), "Game: %s", s->game);
    snprintf(line2_buf, sizeof(line2_buf), "System: %s", s->system_name);

    out_meta->title           = s->title[0] ? s->title : nullptr;
    out_meta->line1           = s->game[0]        ? line1_buf : "";
    out_meta->line2           = s->system_name[0] ? line2_buf : "";
    out_meta->line3           = "";
    out_meta->duration_s      = eng.playerA.GetTotalTime(PLAYTIME_LOOP_INCL | PLAYTIME_TIME_FILE);
    out_meta->num_subsongs    = 1;   // VGM e' un file = una canzone, nessun concetto di subsong
    out_meta->current_subsong = 0;

    return 1;
}

// Il motore non si "mette in pausa" da solo: e' l'host che smette di
// chiamare render() finche' il device e' in pausa (stesso schema di
// sidplayfp/AdPlug/OpenMPT).
static void DUAL_AUDIO_PLUGIN_ABI vgm_pause(void* self)  { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI vgm_resume(void* self) { (void)self; }
static void DUAL_AUDIO_PLUGIN_ABI vgm_stop(void* self) {
    static_cast<vgm_plugin_state_t*>(self)->playing = 0;
}

static int DUAL_AUDIO_PLUGIN_ABI vgm_sample_rate(void* self) {
    return static_cast<vgm_plugin_state_t*>(self)->rate;
}

static int DUAL_AUDIO_PLUGIN_ABI vgm_render(void* self, int16_t* out, int frames) {
    auto* s = static_cast<vgm_plugin_state_t*>(self);
    if(!s->playing || !s->loader) return 0;
    VgmEngine& eng = SharedEngine(s->rate);

    // PlayerA::Render vuole la dimensione in BYTE del buffer, stereo 16-bit
    // interleaved (impostato in SetOutputSettings sopra: 2 canali, 16 bit).
    const UINT32 bytes_wanted = static_cast<UINT32>(frames) * 2 * sizeof(int16_t);
    UINT32 bytes_done = eng.playerA.Render(bytes_wanted, out);
    const int frames_done = static_cast<int>(bytes_done / (2 * sizeof(int16_t)));

#ifdef VGM_PLUGIN_DEBUG
    fprintf(stderr, "[vgm_render] frames=%d bytes_wanted=%u bytes_done=%u frames_done=%d state=0x%02x\n",
            frames, bytes_wanted, bytes_done, frames_done, eng.playerA.GetState());
#endif

    if(eng.playerA.GetState() & PLAYSTATE_FIN) {
        s->playing = 0;
        return frames_done; // ultimo blocco parziale, poi 0 alla chiamata successiva
    }
    return frames_done;
}

static int DUAL_AUDIO_PLUGIN_ABI vgm_is_playing(void* self) {
    return static_cast<vgm_plugin_state_t*>(self)->playing;
}
static int DUAL_AUDIO_PLUGIN_ABI vgm_is_paused(void* self) {
    (void)self;
    return 0; // lo stato "paused" vive nell'host, non nel motore
}

static void DUAL_AUDIO_PLUGIN_ABI vgm_set_volume(void* self, int pct) {
    (void)self; (void)pct; // volume applicato dall'host dopo render()
}
static int DUAL_AUDIO_PLUGIN_ABI vgm_get_volume(void* self) {
    (void)self;
    return 100;
}

// VGM e' un file = una canzone: nessun subsong da cambiare.
static void DUAL_AUDIO_PLUGIN_ABI vgm_set_subsong(void* self, int idx_0based,
                                                   dual_song_meta_t* out_meta) {
    (void)self; (void)idx_0based; (void)out_meta;
}

static double DUAL_AUDIO_PLUGIN_ABI vgm_get_position_seconds(void* self) {
    auto* s = static_cast<vgm_plugin_state_t*>(self);
    if(!s->loader) return 0.0;
    return SharedEngine(s->rate).playerA.GetCurTime(PLAYTIME_TIME_FILE);
}

static int  DUAL_AUDIO_PLUGIN_ABI vgm_can_seek(void* self) { (void)self; return 0; }
static void DUAL_AUDIO_PLUGIN_ABI vgm_seek_seconds(void* self, double seconds) {
    (void)self; (void)seconds;
}

// ── Descrittore statico ───────────────────────────────────────────────────────

static const dual_audio_plugin_t kPlugin = {
    /* info */ {
        DUAL_AUDIO_PLUGIN_TYPE_GENERATOR,
        DUAL_AUDIO_PLUGIN_API_VMAJOR,
        DUAL_AUDIO_PLUGIN_API_VMINOR,
        0, 1, // libvgm non ha versioni numerate — commit e41ca80 (2026)
        "libvgm",
        "libvgm (VGMPlay)",
        "Video Game Music register-log playback (~30 chip emulators)",
        "libvgm\n"
        "Mixed licenses per chip core (BSD-3-Clause / GPL-2.0+, MAME-derived)\n"
        "https://github.com/ValleyBell/libvgm",
        "https://github.com/siriokds/homebrew-dual-audio",
        kExtensions,
        kConfigDialog,
        0.0f, // fade_in_seconds — default di Dual, nessun transiente noto da coprire
        nullptr, // extended_params
    },
    /* ops */ {
        vgm_create, vgm_destroy, vgm_last_error, vgm_can_handle,
        vgm_load, vgm_pause, vgm_resume, vgm_stop,
        vgm_sample_rate, vgm_render,
        vgm_is_playing, vgm_is_paused,
        vgm_set_volume, vgm_get_volume,
        nullptr, // set_option: nessuna opzione configurabile
        vgm_set_subsong, vgm_get_position_seconds,
        vgm_can_seek, vgm_seek_seconds,
    },
};

extern "C" DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
dual_audio_plugin_load(void) {
    return &kPlugin;
}
