project('tll-lua', 'cpp'
	, version: '0.0.0'
	, license: 'MIT'
	, default_options: ['cpp_std=c++2a', 'werror=true', 'optimization=2']
)

include = [include_directories('src')]

fmt = dependency('fmt')
lua = dependency('lua5.3')
tll = dependency('tll')
rst2man = find_program('rst2man', disabler: true, required: false)

shared_library('tll-lua'
	, ['src/module.cc', 'src/measure.cc', 'src/prefix.cc', 'src/tcp.cc']
	, include_directories : include
	, dependencies : [fmt, lua, tll]
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

foreach f : ['lua.rst']
	custom_target('channel-man-@0@'.format(f)
		, input: f
		, output : 'tll-channel-@BASENAME@.7'
		, command : [rst2man, '@INPUT@', '@OUTPUT@']
		, install_dir: get_option('mandir') / 'man7'
		, install: true
	)
endforeach
