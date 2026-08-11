class DualSidplayfp < Formula
  desc "libsidplayfp built as a dylib + plugin adapter, for Dual"
  homepage "https://github.com/siriokds/homebrew-dual-audio/tree/main/modules/sidplayfp"
  # Tarball di release, non un clone git: gia' contiene ./configure
  # pre-generato. Un git checkout + autoreconf (provato prima) scontrava
  # il sandbox di build di Homebrew — "pwd: .: Operation not permitted"
  # seguito da "cannot chdir to : ..." dentro autoreconf, riproducibile
  # solo nella build sandboxata, mai in locale fuori sandbox. Stesso identico
  # url della formula ufficiale "libsidplayfp" (branch stabile, non head).
  url "https://github.com/libsidplayfp/libsidplayfp/releases/download/v2.16.1/libsidplayfp-2.16.1.tar.gz"
  sha256 "ace0f73c2ef8645ab069ce1b298b10e31e36af7b5996109983b2b67ad60ff3ca"
  version "2.16.1"
  license "GPL-2.0-or-later"

  # Nessun conflitto di nome con la formula ufficiale "libsidplayfp" (che
  # installa libsidplayfp.dylib "nudo", senza adattatore plugin) — ma
  # keg_only comunque, per coerenza con dual-uade e per non aggiungere una
  # seconda copia della stessa libreria al prefix condiviso.
  keg_only "provides its own libsidplayfp.dylib + plugin adapter, kept separate"

  depends_on "pkgconf" => :build
  depends_on "libgcrypt"

  def install
    system "./configure", "--disable-silent-rules", *std_configure_args
    system "make", "install"

    # Adattatore dual_audio_plugin.h → libsidplayfp.dylib. Il sorgente vive
    # in questo stesso tap sotto modules/sidplayfp/plugin/, non nel tarball
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
