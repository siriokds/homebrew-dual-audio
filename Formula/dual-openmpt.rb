class DualOpenmpt < Formula
  desc "Plugin adapter for the stock libopenmpt library, for Dual"
  homepage "https://github.com/siriokds/homebrew-dual-audio/tree/main/modules/openmpt"
  # Nessun sorgente esterno da scaricare, stesso schema di dual-adplug: il
  # "url" e' solo formale — Homebrew lo richiede sempre — install() legge
  # l'adattatore da questo stesso tap via __dir__, non dal buildpath scaricato.
  #
  # BSD-3-Clause, come AdPlug il link diretto era gia' legale: questo modulo
  # esiste solo per coerenza architetturale (Dual parla solo con
  # dual_audio_plugin.h, mai direttamente con libopenmpt).
  url "https://github.com/siriokds/homebrew-dual-audio.git", branch: "main"
  version "1.0.0"
  license "BSD-3-Clause"

  livecheck do
    skip "il sorgente e' questo stesso repository, nessuna versione upstream da tracciare"
  end

  depends_on "libopenmpt"

  # Nessuna collisione di nome, keg_only solo per la stessa convenzione degli
  # altri moduli — vedi dual-adplug.rb.
  keg_only "kept under its own prefix, same convention as the other dual-* modules"

  def install
    plugin_src = Pathname.new(__dir__).parent/"modules/openmpt/plugin"
    system ENV.cxx, "-std=c++17", "-shared", "-fPIC",
           "-fvisibility=hidden", "-fvisibility-inlines-hidden",
           "-I#{Formula["libopenmpt"].opt_include}",
           "-L#{Formula["libopenmpt"].opt_lib}", "-lopenmpt",
           "-Wl,-rpath,#{Formula["libopenmpt"].opt_lib}",
           "-o", "libdual_openmpt_plugin.dylib",
           plugin_src/"dual_openmpt_plugin.cpp"
    lib.mkdir
    lib.install "libdual_openmpt_plugin.dylib"
  end

  test do
    assert_predicate lib/"libdual_openmpt_plugin.dylib", :exist?
  end
end
