// BOZZA — non ancora integrata in Dual.
//
// Interfaccia C stabile per plugin audio esterni (dual-uade, dual-sidplayfp, ...).
// Ispirata a deadbeef.h (DB_plugin_t): un solo simbolo esportato per plugin,
// una struct C con versione dell'API separata dalla versione del plugin,
// nessuna dipendenza su C++ ABI o su un GUI toolkit.
//
// Dual non include MAI questo file dentro il codice specifico di un formato:
// lo include solo il loader generico (audio_plugin_loader.cpp) e ogni plugin
// esterno lo include per implementarsi contro di esso.

#pragma once
#include <stdint.h>
#include <stddef.h>

// ── Export / calling convention ─────────────────────────────────────────────
// No-op su macOS/Linux (simboli extern "C" non-static sono già visibili, e il
// calling convention di default coincide su tutta la piattaforma). Su Windows
// diventerebbe la differenza tra una DLL che espone il simbolo o lo nasconde:
//   #define DUAL_AUDIO_PLUGIN_ABI __cdecl
//   #if defined(DUAL_AUDIO_PLUGIN_BUILDING)
//     #define DUAL_AUDIO_PLUGIN_EXPORT __declspec(dllexport)
//   #else
//     #define DUAL_AUDIO_PLUGIN_EXPORT __declspec(dllimport)
//   #endif
// Tenerla qui fin da subito evita di dover ritoccare le firme di tutte le
// funzioni quando arriverà una build Windows.
#if defined(_WIN32)
    #define DUAL_AUDIO_PLUGIN_ABI __cdecl
    #if defined(DUAL_AUDIO_PLUGIN_BUILDING)
        #define DUAL_AUDIO_PLUGIN_EXPORT __declspec(dllexport)
    #else
        #define DUAL_AUDIO_PLUGIN_EXPORT __declspec(dllimport)
    #endif
#else
    #define DUAL_AUDIO_PLUGIN_ABI
    #define DUAL_AUDIO_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ── Versione dell'API (non del plugin) ──────────────────────────────────────
// Un plugin dichiara contro quale api_vmajor/vminor è stato compilato.
// Dual rifiuta con un messaggio chiaro un plugin con vmajor diverso, e ignora
// campi/funzioni oltre la vminor che non conosce (retrocompatibilità additiva,
// stesso principio di DDB_API_LEVEL in deadbeef.h).
#define DUAL_AUDIO_PLUGIN_API_VMAJOR 1
// v1.1: aggiunto info.fade_in_seconds (sessione 2026-08-11). Additivo per
// costruzione: un host che legge solo fino a vminor 0 non tocca quel campo,
// un plugin vecchio compilato a vminor 0 lo lascia a zero-init, che l'host
// interpreta correttamente come "usa il default" (vedi il campo stesso).
//
// v1.2: set_subsong() guadagna il parametro out_meta (stessa sessione).
// A differenza di v1.1 questa NON e' una modifica additiva sicura — cambia
// la firma di una funzione della vtable, non aggiunge un campo in coda a
// una struct. Vale solo perche' tutti gli adattatori vengono ricompilati
// insieme a questo header ad ogni bump; un vero terzo plugin precompilato
// smetterebbe di funzionare.
//
// v1.3: aggiunta ops.set_option(self, key, value) — vedi il commento sulla
// funzione. Stessa clausola di v1.2: e' un campo NUOVO in coda alla vtable,
// non un campo in coda a una struct di soli dati, quindi vale solo perche'
// tutti gli adattatori si ricompilano insieme a questo header. Un host che
// legge un plugin compilato a vminor < 3 non deve chiamare set_option: il
// puntatore a funzione, oltre la fine della struct realmente allocata dal
// plugin vecchio, e' memoria che non gli appartiene.
#define DUAL_AUDIO_PLUGIN_API_VMINOR 3

// ── Architettura ─────────────────────────────────────────────────────────────
// Non serve un campo esplicito: dlopen() su macOS rifiuta già da solo un
// dylib compilato per l'architettura sbagliata (dlerror() lo spiega). Il
// loader intercetta quel fallimento e lo riporta come "incompatibile con
// questo Mac" invece di far sparire il plugin silenziosamente.

typedef struct dual_audio_plugin_s dual_audio_plugin_t;

// ── Ruolo del plugin ─────────────────────────────────────────────────────────
// Oggi esiste solo GENERATOR (decodifica file → PCM, la vtable in
// dual_audio_plugin_ops_t è tagliata su questa forma: create/load/render).
// EFFECT e OUTPUT sono riservati per quando servirà un secondo tipo di vtable
// (es. process(in, out, frames) per un DSP, open_device/write per un output
// alternativo a AudioOutput) — stesso principio del type in DB_plugin_t di
// DeaDBeeF, che sceglie fra DB_decoder_t/DB_output_t/DB_dsp_t in base a
// questo campo. Il loader di Dual legge type PRIMA di interpretare ops,
// e oggi rifiuta qualunque cosa non sia GENERATOR (nessun'altra forma
// esiste ancora da interpretare).
typedef enum {
    DUAL_AUDIO_PLUGIN_TYPE_GENERATOR = 0, // unico tipo implementato oggi
    DUAL_AUDIO_PLUGIN_TYPE_EFFECT    = 1, // riservato, non ancora supportato
    DUAL_AUDIO_PLUGIN_TYPE_OUTPUT    = 2, // riservato, non ancora supportato
} dual_audio_plugin_type_t;

// ── Metadati — quello che Dual mostra nell'about/dependency checker ─────────
typedef struct {
    dual_audio_plugin_type_t type;

    uint16_t api_vmajor;
    uint16_t api_vminor;

    uint16_t version_major;   // versione del plugin stesso, non dell'API
    uint16_t version_minor;

    const char* id;           // "uade", "sidplayfp" — usato internamente, stabile
    const char* name;         // "UADE (dragnet fork)" — mostrato all'utente
    const char* descr;        // una riga
    const char* copyright;    // licenza per esteso, multi-riga (come adplug.c)
    const char* website;      // opzionale, può essere NULL

    // Estensioni gestite, terminata da NULL. Il loader la usa per costruire
    // la mappa estensione→plugin senza che il plugin sappia nulla del resto.
    const char* const* extensions;

    // Mini-DSL per la pagina di configurazione — oggi un solo tipo di
    // controllo, un menu a tendina. Una riga per controllo, campi separati
    // da '|', opzioni scritte "chiave:descrizione" (chiave stabile e non
    // tradotta, salvata in configurazione; descrizione mostrata all'utente):
    //
    //   select|<etichetta>|<chiave_config>|<chiave_default>|<k1>:<d1>|<k2>:<d2>|...
    //
    // Esempio reale (AdPlug, motore di sintesi OPL):
    //   "select|OPL Engine|opl_engine|emu|"
    //   "emu:CEmuopl (default)|nuked:Nuked OPL3 (cycle-accurate)|"
    //   "ken:Ken Silverman's|satoh:Tatsuyuki Satoh's|woody:DOSBox"
    //
    // La chiave (non l'indice posizionale) e' cio' che Dual salva e passa a
    // ops.set_option: un indice si romperebbe silenziosamente se l'ordine
    // delle opzioni cambiasse in una versione futura del plugin, una chiave
    // testuale no (stesso motivo per cui l'output audio si salva per id, non
    // per handle — vedi AudioDeviceConfig in Dual). Dual la interpreta e
    // disegna i widget in wx; il plugin non disegna nulla, e non sa nulla di
    // wxWidgets. NULL se il plugin non ha parametri configurabili.
    const char* config_dialog;

    // Quanto deve durare la salita anti-click quando Dual avvia l'uscita
    // audio per questo plugin, in secondi. <= 0 (incluso lo zero-init di un
    // plugin compilato a vminor 0, prima che questo campo esistesse) →
    // Dual usa il proprio default (vedi kAudioFadeInSeconds). Esiste perche'
    // alcuni motori hanno un transiente d'attacco che il default non copre
    // (sidplayfp: routine INIT che scrive i registri filtro/volume qualche
    // frame dopo l'avvio, non al frame zero — 40 ms, ~2 raster PAL, coprono
    // meglio senza diventare un fade-in udibile).
    float fade_in_seconds;

    // ── Estensione futura (stile pNext di Vulkan / catene di struct DirectX) ──
    // Riservato: DEVE essere NULL finché non esiste una versione 2 di questa
    // struct. Il giorno in cui serviranno altri campi, invece di riaprire
    // questo layout (che romperebbe ogni plugin già compilato) si punterà da
    // qui a una dual_audio_plugin_info_v2_t che comincia essa stessa con un
    // proprio tag di versione. Un host vecchio, non sapendo cosa sia, lo
    // ignora semplicemente (non lo deferenzia mai) — nessuna rottura in
    // nessuna delle due direzioni, aggiuntivo puro.
    const void* extended_params;
} dual_audio_plugin_info_t;

// ── Song metadata — normalizzato, indipendente dal formato sorgente ─────────
typedef struct {
    const char* title;
    const char* line1;        // es. "Format: ProTracker 2.x"
    const char* line2;        // es. "Player: mod.noplayer"
    const char* line3;        // es. "22.3 KB · 2:15 · 3 subsongs"
    double      duration_s;   // 0 = sconosciuta
    int         num_subsongs;
    int         current_subsong; // 0-based
} dual_song_meta_t;

// ── Funzioni operative — stesso ruolo di IAudioBackend, in C ────────────────
// "self" è l'handle opaco restituito da create(); ogni plugin decide cosa
// mettere dietro (nel caso di dual-uade, wrapperebbe il proprio AudioPlayer).
// Ogni puntatore a funzione dichiara esplicitamente DUAL_AUDIO_PLUGIN_ABI: su
// macOS/Linux è un no-op, su Windows fissa __cdecl sia lato host che lato
// plugin — altrimenti un mismatch di calling convention tra i due lati del
// confine corromperebbe lo stack silenziosamente.
typedef struct {
    // preferred_rate_hz è il rate del device audio di Dual (fisso per tutta
    // la sessione, non cambia da un plugin all'altro). Un plugin che sa
    // ricampionare a piacere (come UADE, vedi UC_FREQUENCY in
    // audio_player.cpp) può renderizzare direttamente lì; uno che ha un rate
    // nativo fisso (es. un chip YM) può ignorare l'hint e renderizzare al suo
    // rate — sample_rate() dichiara sempre la verità su cosa produce
    // davvero. Dual, non il plugin, si occupa di ricampionare la differenza:
    // vedi la nota sul ricampionatore generico più sotto.
    //
    // create() può fallire (es. il proprio backend nativo non riesce ad
    // inizializzarsi — libuade assente, sample rate non supportato) e
    // ritornare NULL: last_error() DEVE restare leggibile anche con self ==
    // NULL in quel caso specifico (usare uno stato statico o thread-local
    // per l'ultimo errore di create(), non uno per-istanza).
    void* (DUAL_AUDIO_PLUGIN_ABI *create)(int preferred_rate_hz);
    void  (DUAL_AUDIO_PLUGIN_ABI *destroy)(void* self);

    // Messaggio dell'ultimo errore (create() fallita, oppure load() tornata
    // 0): stesso ruolo di IAudioBackend::OpenError() + AudioPlayer::on_error
    // in Dual oggi, unificati in una sola funzione pull invece di due
    // callback. Stringa statica/thread-local del plugin, valida solo fino
    // alla chiamata successiva — Dual la copia subito se deve mostrarla.
    const char* (DUAL_AUDIO_PLUGIN_ABI *last_error)(void* self);

    // true se il plugin può gestire davvero il file (probe leggero, non apre
    // ancora lo stream) — non basta guardare l'estensione: un .mod può essere
    // gestito da openmpt O da uade a seconda del formato interno.
    int   (DUAL_AUDIO_PLUGIN_ABI *can_handle)(void* self, const char* path);

    int   (DUAL_AUDIO_PLUGIN_ABI *load)(void* self, const char* path, dual_song_meta_t* out_meta);
    void  (DUAL_AUDIO_PLUGIN_ABI *pause)(void* self);
    void  (DUAL_AUDIO_PLUGIN_ABI *resume)(void* self);
    void  (DUAL_AUDIO_PLUGIN_ABI *stop)(void* self);

    // Sample rate del PCM che render() produce per QUESTO song — non è detto
    // coincida con preferred_rate_hz passato a create() (dipende se il
    // plugin sa/vuole onorarlo), ma dopo load() è fissa per tutta la durata
    // del brano corrente. Il device audio di Dual resta aperto UNA VOLTA a
    // preferred_rate_hz per tutta la sessione — non si riapre mai per
    // inseguire un plugin. Se sample_rate() != preferred_rate_hz, il loader
    // generico di Dual interpone il proprio ricampionatore (DSP generico,
    // non specifico di alcun formato) fra render() e il device.
    int   (DUAL_AUDIO_PLUGIN_ABI *sample_rate)(void* self);

    // Pull: Dual chiama questa dal proprio thread audio per riempire "out"
    // (16-bit stereo interleaved, come kBytesPerFrame in audio_player.cpp) fino
    // a "frames", al sample_rate() dichiarato sopra — mai a preferred_rate_hz
    // se il plugin non l'ha onorato: il ricampionamento verso il device è
    // sempre responsabilità del loader in Dual, mai del plugin che chiama
    // questa funzione. Ritorna il numero di frame scritti; 0 = fine canzone;
    // <0 = errore fatale del backend. Il plugin non possiede né apre mai un
    // device audio: quello resta un'esclusiva di Dual (stesso ruolo di Impl::Render
    // in audio_player.cpp oggi, solo dietro questa vtable).
    int   (DUAL_AUDIO_PLUGIN_ABI *render)(void* self, int16_t* out, int frames);

    int   (DUAL_AUDIO_PLUGIN_ABI *is_playing)(void* self);
    int   (DUAL_AUDIO_PLUGIN_ABI *is_paused)(void* self);

    void  (DUAL_AUDIO_PLUGIN_ABI *set_volume)(void* self, int pct);
    int   (DUAL_AUDIO_PLUGIN_ABI *get_volume)(void* self);

    // Applica un'opzione dichiarata dal plugin stesso nel proprio
    // info.config_dialog (v1.3). "key" e' la <chiave_config> di una riga
    // "select", "value" una delle <k1>/<k2>/... di quella riga — mai
    // testo libero, Dual passa solo cio' che il plugin ha dichiarato.
    // Dual la chiama sempre DOPO create() e PRIMA di load(): un plugin che
    // ha bisogno del valore per il render (es. quale motore OPL istanziare)
    // lo applica li', non a caldo durante la riproduzione. Puo' essere NULL
    // (nessuna opzione configurabile) — Dual controlla il puntatore prima
    // di chiamarlo, oltre a api_vminor >= 3.
    void  (DUAL_AUDIO_PLUGIN_ABI *set_option)(void* self, const char* key, const char* value);

    // out_meta va riempito con i metadati AGGIORNATI del nuovo subsong
    // (stesso ruolo di load()) — senza, l'host non avrebbe modo di sapere
    // che titolo/autore sono cambiati passando da un subsong all'altro
    // (bug reale: mancava del tutto nella prima versione di quest'ABI,
    // scoperto scrivendo l'adattatore OpenMPT). Firma diversa da quella
    // originale: a differenza dei campi aggiunti in coda alla struct info,
    // un cambio di firma di funzione NON e' compatibile con un plugin gia'
    // compilato — qui va bene solo perche' tutti gli adattatori esistenti
    // vengono ricompilati insieme a questo header, non e' un caso
    // "additivo sicuro" come extended_params.
    void  (DUAL_AUDIO_PLUGIN_ABI *set_subsong)(void* self, int idx_0based,
                                                dual_song_meta_t* out_meta);
    double (DUAL_AUDIO_PLUGIN_ABI *get_position_seconds)(void* self);

    int   (DUAL_AUDIO_PLUGIN_ABI *can_seek)(void* self);
    void  (DUAL_AUDIO_PLUGIN_ABI *seek_seconds)(void* self, double seconds);
} dual_audio_plugin_ops_t;

struct dual_audio_plugin_s {
    dual_audio_plugin_info_t info;
    dual_audio_plugin_ops_t  ops;
};

// ── Unico simbolo esportato dal modulo (.dylib / .so / .dll) ────────────────
// Nome fisso, risolto via dlsym/GetProcAddress da Dual dopo il dlopen/
// LoadLibrary. Ritorna un puntatore statico (vive per tutta la durata del
// processo, come in deadbeef). Il plugin lo dichiara così:
//   DUAL_AUDIO_PLUGIN_EXPORT const dual_audio_plugin_t* DUAL_AUDIO_PLUGIN_ABI
//   dual_audio_plugin_load(void) { ... }
typedef const dual_audio_plugin_t* (DUAL_AUDIO_PLUGIN_ABI *dual_audio_plugin_load_fn)(void);
#define DUAL_AUDIO_PLUGIN_ENTRY_SYMBOL "dual_audio_plugin_load"

#ifdef __cplusplus
}
#endif
