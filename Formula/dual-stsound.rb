class DualStsound < Formula
  desc "StSoundLibrary (vendored) compiled as a plugin adapter, for Dual"
  homepage "https://github.com/siriokds/homebrew-dual-audio/tree/main/modules/stsound"
  # Nessun sorgente esterno da scaricare a build-time: modules/stsound/src/
  # e' gia' un vendoring diretto (non un submodule — il repo a monte,
  # arnaud-carre/StSound, non e' mantenuto da anni) committato in questo
  # stesso tap. Il "url" qui e' solo formale — Homebrew lo richiede sempre.
  #
  # MIT (verificato sul LICENSE del vendoring — il credito in Dual dice
  # ancora "BSD-2-Clause", disallineato, da correggere separatamente):
  # nessun obbligo di isolamento, migrato solo per coerenza architetturale
  # (Dual parla solo con dual_audio_plugin.h). A differenza di
  # dual-adplug/dual-openmpt pero' non esiste una libreria "stock" da cui
  # dipendere — StSound e' sempre stato pensato per essere compilato
  # dentro il progetto che lo usa, mai distribuito come libreria a se'.
  url "https://github.com/siriokds/homebrew-dual-audio.git", branch: "main"
  version "1.0.0"
  license "MIT"

  livecheck do
    skip "il sorgente e' vendorizzato in questo stesso repository, nessuna versione upstream da tracciare"
  end

  # Nessuna dipendenza: StSound non usa nulla oltre alla libc++ di sistema.
  keg_only "kept under its own prefix, same convention as the other dual-* modules"

  def install
    # Compila direttamente qui invece di richiamare build-macos.sh: quello
    # script scrive nella propria cartella (per l'uso manuale, fuori da
    # brew) — dentro il sandbox di build va tutto nel buildpath di Homebrew.
    module_root = Pathname.new(__dir__).parent/"modules/stsound"
    src = module_root/"src/StSoundLibrary"

    system ENV.cxx, "-std=c++17", "-shared", "-fPIC", "-O2",
           "-fvisibility=hidden", "-fvisibility-inlines-hidden",
           "-I#{src}", "-I#{module_root}/plugin",
           "-o", "libdual_stsound_plugin.dylib",
           module_root/"plugin/dual_stsound_plugin.cpp",
           src/"digidrum.cpp",
           src/"Ymload.cpp",
           src/"Ym2149Ex.cpp",
           src/"YmUserInterface.cpp",
           src/"YmMusic.cpp",
           src/"LZH/LzhLib.cpp"
    lib.mkdir
    lib.install "libdual_stsound_plugin.dylib"
  end

  test do
    assert_predicate lib/"libdual_stsound_plugin.dylib", :exist?
  end
end
