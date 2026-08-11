class DualSidplayfp < Formula
  desc "libsidplayfp built as a dylib + plugin adapter, for Dual"
  homepage "https://github.com/siriokds/homebrew-dual-audio/tree/main/modules/sidplayfp"
  url "https://github.com/libsidplayfp/libsidplayfp.git",
      tag:      "v2.16.1",
      revision: "790f05841818877eb97e0112ac97c6e974ad5468", # coerente col submodule del tap
      using:    :git
  version "2.16.1"
  license "GPL-2.0-or-later"

  # Nessun conflitto di nome col la formula ufficiale "libsidplayfp" (che
  # installa libsidplayfp.dylib "nudo", senza adattatore plugin) — ma
  # keg_only comunque, per coerenza con dual-uade e per non aggiungere una
  # seconda copia della stessa libreria al prefix condiviso.
  keg_only "provides its own libsidplayfp.dylib + plugin adapter, kept separate"

  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "coreutils" => :build # GNU od, richiesto da psiddrv.bin
  depends_on "libtool"   => :build
  depends_on "pkgconf"   => :build
  depends_on "xa"        => :build # cross-assembler 6502, per psiddrv
  depends_on "libgcrypt"
  depends_on "libusb"

  def install
    # NON aggiungere gnubin di coreutils al PATH: sostituirebbe anche
    # /bin/pwd con quello GNU, che va in conflitto col sandbox di build di
    # Homebrew ("pwd: .: Operation not permitted", poi autoreconf fallisce
    # a fine corsa con "cannot chdir to : ..." perche' la pwd iniziale e'
    # vuota). Non serve comunque: configure.ac cerca sia "od" che "god"
    # (AC_PATH_PROGS_FEATURE_CHECK), e "god" — l'alias prefissato di
    # coreutils — e' gia' nel PATH grazie al solo depends_on sopra, senza
    # bisogno di toccare l'ordine di risoluzione dei binari di sistema.

    # Il sorgente ha tre submodule annidati (resid, driver exSID/USBSID —
    # vedi .gitmodules a monte): Homebrew non li scarica da solo con un
    # plain "url ... using: :git", vanno inizializzati esplicitamente prima
    # di autoreconf (che altrimenti non trova le macro m4 del driver exSID).
    system "git", "submodule", "update", "--init", "--recursive"

    # Il sandbox di build di Homebrew nega l'accesso (getcwd/pwd) al file
    # .git di un submodule, che punta fuori dal buildpath autorizzato
    # (dentro .git/modules/ del genitore) — sintomo: "pwd: .: Operation
    # not permitted" seguito da "cannot chdir to : ..." dentro autoreconf.
    # Il contenuto serve solo come sorgente, non la sua storia git: si
    # rimuovono i puntatori .git dei submodule annidati, diventano directory
    # normali e il sandbox non ci inciampa piu'.
    Dir.glob("src/builders/*/{resid,driver}/.git").each { |f| rm_rf f }

    system "autoreconf", "--force", "--install", "--verbose"
    system "./configure", "--disable-silent-rules", *std_configure_args
    system "make", "install"

    # Adattatore dual_audio_plugin.h → libsidplayfp.dylib. Il sorgente vive
    # in questo stesso tap sotto modules/sidplayfp/plugin/, non nell'url
    # scaricato (che e' solo libsidplayfp upstream, invariato).
    plugin_src = Pathname.new(__dir__).parent/"modules/sidplayfp/plugin"
    cp plugin_src/"dual_sidplayfp_plugin.cpp", buildpath
    cp plugin_src/"dual_audio_plugin.h", buildpath
    system ENV.cxx, "-std=c++17", "-shared", "-fPIC",
           "-fvisibility=hidden", "-fvisibility-inlines-hidden",
           "-I#{include}", "-L#{lib}", "-lsidplayfp",
           "-Wl,-rpath,#{lib}",
           "-o", "libdual_sidplayfp_plugin.dylib",
           buildpath/"dual_sidplayfp_plugin.cpp"
    lib.install "libdual_sidplayfp_plugin.dylib"
  end

  test do
    assert_predicate lib/"libsidplayfp.dylib", :exist?
    assert_predicate lib/"libdual_sidplayfp_plugin.dylib", :exist?
  end
end
