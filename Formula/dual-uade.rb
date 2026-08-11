class DualUade < Formula
  desc "UADE (dragnet fork) with extra Amiga format support, built for Dual"
  homepage "https://github.com/siriokds/dual-uade"
  url "https://gitlab.com/mvtiaine/uade.git", branch: "dragnet"
  version "3.05-dragnet"
  license "GPL-2.0-only"

  # Installs the same binary/library names as the official "uade" formula
  # (uade123, libuade.dylib) — keg_only keeps it out of the shared prefix so
  # both can be installed side by side without conflicting, and callers pick
  # which libuade.dylib to load explicitly.
  keg_only "conflicts with the official uade formula (same binary/library names)"

  depends_on "pkgconf" => :build
  depends_on "libao"

  resource "libzakalwe" do
    url "https://gitlab.com/hors/libzakalwe/-/archive/v1.0.0/libzakalwe-v1.0.0.tar.bz2"
    sha256 "cb503c557b04f34069654083963a056deb85a6dea25ba4b69aaaa2bbf7290a98"
  end

  resource "bencode-tools" do
    url "https://gitlab.com/heikkiorsila/bencodetools/-/archive/v1.0.1/bencodetools-v1.0.1.tar.bz2"
    sha256 "e41ae682525cf335b5f5ec0ba9b954abfe7b448e8ed13e2aa2a44e49fce2ca12"
  end

  def install
    lib.mkdir # for libzakalwe

    resource("libzakalwe").stage do
      # Xcode 14.3+ rejects implicit function declarations by default.
      if DevelopmentTools.clang_build_version >= 1403
        inreplace "Makefile", "CFLAGS = -W -Wall", "CFLAGS = -Wno-implicit-function-declaration -W -Wall"
      end
      inreplace "Makefile", "-Wl,-soname,$@", "-Wl"
      system "./configure", *std_configure_args
      # CC in this Makefile defaults to "cgcc" (Sparse's checker wrapper,
      # not installed here) — override it on the same invocation that
      # builds; a separate plain `make` first would use the wrong default.
      system "make", "install", "PREFIX=#{prefix}", "CC=#{ENV.cc}"
    end

    resource("bencode-tools").stage do
      system "./configure", "--prefix=#{prefix}", "--without-python"
      system "make"
      system "make", "install"
    end

    system "./configure", "--prefix=#{prefix}",
                          "--libzakalwe-prefix=#{prefix}",
                          "--without-write-audio"
    system "make", "install"
  end

  test do
    assert_predicate lib/"libuade.dylib", :exist?
    assert_match version.to_s, shell_output("#{bin}/uade123 --version")
  end
end
