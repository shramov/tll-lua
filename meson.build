project('tll-lua', 'cpp'
	, version: '0.7.1'
	, license: 'MIT'
	, default_options: ['cpp_std=c++2a', 'werror=true', 'optimization=2']
)

include = [include_directories('src')]

fmt = dependency('fmt')
lua = dependency('lua5.3')
tll = dependency('tll')
rst2man = find_program('rst2man', disabler: true, required: false)
bats = find_program('bats', disabler: true, required: false)
dl = meson.get_compiler('cpp').find_library('dl')

shared_library('tll-lua'
	, ['src/module.cc', 'src/measure.cc', 'src/prefix.cc', 'src/tcp.cc', 'src/logic.cc', 'src/forward.cc']
	, include_directories : include
	, dependencies : [fmt, lua, tll, dl]
	, install : true
)

install_subdir('src/tll', install_dir : get_option('includedir'))

benchmark('bench', executable('bench'
		, ['src/bench.cc']
		, include_directories : include
		, dependencies : [fmt, lua, tll]
		)
	, workdir : meson.current_source_dir()
)

gtest = dependency('gtest')

if gtest.found()
	test('test-reflection', executable('test-reflection',
		sources: ['src/test-reflection.cc'],
		include_directories: include,
		dependencies: [fmt, gtest, lua, tll],
		)
	)
endif

test('pytest', import('python').find_installation('python3')
	, args: ['-m', 'pytest', '-v', 'tests']
	, env: 'BUILD_DIR=@0@'.format(meson.current_build_dir())
	, workdir: meson.current_source_dir()
)

test('test-cli', bats, args: ['tests/test_read.sh'], workdir: meson.current_source_dir())

foreach f : ['read', 'convert']
  custom_target('tll-' + f
          , output : 'tll-' + f + '.1'
          , input : ['doc' / f + '.rst', 'doc' / f + '-options.rst']
          , command : [rst2man, '@INPUT0@', '@OUTPUT@']
          , install_dir: get_option('mandir') / 'man1'
          , install: true
  )
endforeach

foreach f : ['doc/lua.rst']
	custom_target('channel-man-@0@'.format(f)
		, input: f
		, output : 'tll-channel-@BASENAME@.7'
		, command : [rst2man, '@INPUT@', '@OUTPUT@']
		, install_dir: get_option('mandir') / 'man7'
		, install: true
	)
endforeach
