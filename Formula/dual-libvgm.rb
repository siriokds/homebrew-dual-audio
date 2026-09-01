class DualLibvgm < Formula
  desc "libvgm (VGMPlay engine, vendored) compiled as a plugin adapter, for Dual"
  homepage "https://github.com/siriokds/homebrew-dual-audio/tree/main/modules/libvgm"
  # Nessun sorgente esterno da scaricare a build-time: modules/libvgm/src/ e'
  # gia' un vendoring diretto (ValleyBell/libvgm, commit e41ca80, nessuna
  # release taggata a monte da poter referenziare con un url+sha256 vero)
  # committato in questo stesso tap, stesso schema di dual-stsound. Il "url"
  # qui e' solo formale — Homebrew lo richiede sempre.
  #
  # Licenza mista per-file, convenzione MAME (`// license: ...` in testa a
  # ogni core in emu/cores/*.c): BSD-3-Clause e GPL-2.0+ affiancati (es.
  # ay8910.c e c352.c sono BSD-3-Clause, fmopn.c e fmopl.c sono GPL-2.0+).
  # Isolato come plugin comunque, stesso motivo di dual-sidplayfp: il tap
  # nel suo complesso e' gia' GPL (per via di sidplayfp), quindi i core
  # GPL-2.0+ qui non aggiungono nessuna tensione di licenza nuova.
  url "https://github.com/siriokds/homebrew-dual-audio.git", branch: "main"
  version "1.0.1"
  license "GPL-2.0-or-later" # licenza piu' restrittiva fra quelle usate, per coerenza col resto del tap

  livecheck do
    skip "il sorgente e' vendorizzato in questo stesso repository, nessuna versione upstream da tracciare"
  end

  # zlib/iconv/pthread sono di sistema su macOS (CMake li trova negli SDK
  # Apple, nessun pacchetto Homebrew necessario) — nessuna depends_on.
  keg_only "kept under its own prefix, same convention as the other dual-* modules"

  depends_on "cmake" => :build

  def install
    module_root = Pathname.new(__dir__).parent/"modules/libvgm"
    src = module_root/"src"

    # 1. Builda solo i tre target che servono (chip cores + utility I/O +
    #    parser/sequencer VGM/GYM/S98/DRO) — non il player CLI ne' i test,
    #    che tirerebbero dentro libao e altre dipendenze audio inutili qui.
    system "cmake", "-S", src, "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release", "-DUTIL_CHARCNV_ICONV=ON"
    system "cmake", "--build", "build", "--target", "vgm-emu", "vgm-utils", "vgm-player",
           "-j", ENV.make_jobs.to_s

    # 2. Compila l'adattatore e linka contro le tre librerie statiche appena
    #    prodotte, stesso pattern di dual-stsound (compilazione diretta,
    #    niente install upstream).
    system ENV.cxx, "-std=c++17", "-shared", "-fPIC",
           "-fvisibility=hidden", "-fvisibility-inlines-hidden",
           "-I#{src}",
           "-o", "libdual_libvgm_plugin.dylib",
           module_root/"plugin/dual_libvgm_plugin.cpp",
           "build/bin/libvgm-player.a",
           "build/bin/libvgm-emu.a",
           "build/bin/libvgm-utils.a",
           "-lz", "-liconv", "-lpthread"
    lib.mkdir
    lib.install "libdual_libvgm_plugin.dylib"
  end

  test do
    assert_predicate lib/"libdual_libvgm_plugin.dylib", :exist?
  end
end
