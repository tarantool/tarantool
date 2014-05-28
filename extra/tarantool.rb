require 'formula'

class Tarantool < Formula
  homepage 'http://tarantool.org'

  depends_on 'cmake'    => :build
  depends_on "readline" => :build

  option 'with-debug', "Build Debug version"
  option 'with-tests', "Run Tests after building"

  stable do
    url 'https://github.com/tarantool/tarantool.git', :using => :git, :branch => "stable"
    depends_on 'e2fsprogs' => :recommended
    if build.include? 'with-tests'
      depends_on 'python-daemon' => [:python, "daemon",  :build]
      depends_on 'pyyaml'        => [:python, "yaml",    :build]
      depends_on 'pexpect'       => [:python, "pexpect", :build]
    end
    version "1.5"
  end

  devel do
    url 'https://github.com/tarantool/tarantool.git', :using => :git, :branch => "master"
    depends_on 'e2fsprogs' => :build
    if build.include? 'with-tests'
      depends_on 'python-daemon' => [:python, "daemon", :build]
      depends_on 'pyyaml'        => [:python, "yaml",   :build]
    end
    version "1.6"
  end

  def install
    args = []
    if build.include? 'with-debug'
      ENV.enable_warnings
      ENV.deparallelize
      args << ["-DCMAKE_BUILD_TYPE=Debug"]
      ohai "Building with Debug"
    else
      args << ["-DCMAKE_BUILD_TYPE=Release"]
      ohai "Building with Release"
    end
    args << "-DENABLE_CLIENT=True" if build.stable?
    args += std_cmake_args
    
    ohai "Preparing"
    version = `git -C #{cached_download} describe HEAD`
    File.open("#{buildpath}/VERSION", 'w') {|file| file.write(version)}

    ohai "Configuring:"
    system "cmake", ".", *args

    ohai "Building:"
    system "make"

    ohai "Installing:"
    system "make install"

    ohai "Installing man"
    man1.install 'doc/man/tarantool.1'
    if build.stable?
      man1.install 'doc/man/tarantool_box.1'
    end

    ohai "Installing config"
    if build.stable?
      inreplace etc/"tarantool.cfg", /^work_dir =.*/, "work_dir = #{prefix}/var/lib/tarantool"
    else
      doc.install "test/box/box.lua"
      inreplace doc/"box.lua" do |s|
          s.gsub!(/^os = require.*\n/     , '')
          s.gsub!(/(primary_port\s*=).*/, '\1 3301,')
          s.gsub!(/(admin_port\s*=).*/  , '\1 3313,')
          s.gsub!(/(rows_per_wal.*)/    , '\1,')
          s.gsub!(/^}.*/                , "\twork_dir\t\t\t= \"#{prefix}/var/lib/tarantool\",\n}")
      end
    end

    if build.include? 'with-tests'
        ohai "Testing Tarantool with internal test suite:"
        system "/usr/bin/env", "python", "#{buildpath}/test/test-run.py", "--builddir", "#{buildpath}", "--vardir", "#{buildpath}/test/var"
    end
  end

  def test
    system "#{bin}/tarantool", "--version"
  end
end
