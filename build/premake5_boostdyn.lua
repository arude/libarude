boost = {}

boost.root = os.getenv( "BOOST_ROOT" )
boost.defines = { "BOOST_ALL_DYN_LINK", "BOOST_SPIRIT_THREADSAFE", "WIN32_LEAN_AND_MEAN" }


if os.is( "windows" ) then
  if not boost.root then
    error( "missing the BOOST_ROOT environment variable" )
  end
  includedirs = boost.root
  configuration "x64"
    libdirs { boost.root .. "/lib64" }
  configuration "not x64"
    libdirs { boost.root .. "/lib" }
else if os.is( "linux" ) then
  includedirs = "/usr/include/"
else then
  error( "Unsupported OS!" )
end