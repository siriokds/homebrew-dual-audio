class DualAdplug < Formula
  desc "Plugin adapter for the stock AdPlug library, for Dual"
  homepage "https://github.com/siriokds/homebrew-dual-audio/tree/main/modules/adplug"
  # Nessun sorgente esterno da scaricare: l'unico "sorgente" e' l'adattatore
  # stesso, che vive in questo stesso tap (modules/adplug/plugin/, letto via
  # __dir__ in install, come fanno gia' dual-uade/dual-sidplayfp per le loro
  # rispettive cartelle plugin/). Il "url" qui e' solo formale — Homebrew lo
  # richiede sempre — non e' cio' che install() usa davvero.
  #
  # A differenza di dual-uade (fork) e dual-sidplayfp (isolamento GPL), qui
  # non serve ne' un fork ne' isolare la licenza — AdPlug e' LGPL, il link
  # diretto e' gia' legale. L'unico motivo di questo modulo e' la coerenza
  # architetturale: Dual parla solo con dual_audio_plugin.h, mai
  # direttamente con AdPlug.
  url "https://github.com/siriokds/homebrew-dual-audio.git", branch: "main"
  version "1.0.0"
  license "LGPL-2.1-or-later"

  # Nessuna collisione di nome con nulla (a differenza di dual-uade): serve
  # solo a mantenere la stessa convenzione degli altri moduli — ogni
  # dual-<nome> vive sotto /opt/homebrew/opt/dual-<nome>/lib/, che e'
  # esattamente cio' che audio_plugin_loader.cpp scandisce lato Dual.
  keg_only "kept under its own prefix, same convention as the other dual-* modules"

  livecheck do
    skip "il sorgente e' questo stesso repository, nessuna versione upstream da tracciare"
  end

  depends_on "adplug"
  depends_on "libbinio"

  def install
    plugin_src = Pathname.new(__dir__).parent/"modules/adplug/plugin"
    system ENV.cxx, "-std=c++17", "-shared", "-fPIC",
           "-fvisibility=hidden", "-fvisibility-inlines-hidden",
           "-I#{Formula["adplug"].opt_include}",
           "-I#{Formula["libbinio"].opt_include}/libbinio",
           "-L#{Formula["adplug"].opt_lib}", "-ladplug",
           "-L#{Formula["libbinio"].opt_lib}", "-lbinio",
           "-Wl,-rpath,#{Formula["adplug"].opt_lib}",
           "-Wl,-rpath,#{Formula["libbinio"].opt_lib}",
           "-o", "libdual_adplug_plugin.dylib",
           plugin_src/"dual_adplug_plugin.cpp"
    lib.mkdir
    lib.install "libdual_adplug_plugin.dylib"
  end

  test do
    assert_predicate lib/"libdual_adplug_plugin.dylib", :exist?
  end
end
