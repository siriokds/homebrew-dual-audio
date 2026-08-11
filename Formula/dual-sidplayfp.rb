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
    # coreutils davanti nel PATH: l'od di sistema su macOS e' BSD e non
    # supporta -w, che configure vuole per generare psiddrv.bin.
    ENV.prepend_path "PATH", Formula["coreutils"].opt_libexec/"gnubin"

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
