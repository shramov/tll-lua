project('tll-lua', 'cpp'
	, version: '0.0.0'
	, license: 'MIT'
	, default_options: ['cpp_std=c++17', 'werror=true', 'optimization=2']
)

include = [include_directories('src')]

fmt = dependency('fmt')
lua = dependency('lua5.3')
tll = dependency('tll')

shared_library('tll-lua'
	, ['src/channel.cc', 'src/filter.cc']
	, include_directories : include
	, dependencies : [fmt, lua, tll]
	, install : true
)

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
