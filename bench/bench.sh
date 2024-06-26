#!/bin/sh

exec ~/src/tll/build/bench/tll-bench-channel -m build/tll-lua --count=1000000 --msgid=10 \
	'lua+null://;code=file://bench/forward.lua;scheme=yaml://bench/scheme.yaml' \
	'lua+null://;code=file://bench/table.lua;scheme=yaml://bench/scheme.yaml' \
	'lua+null://;code=file://bench/simple.lua;scheme=yaml://bench/scheme.yaml' \
	'lua+null://;code=file://bench/nested.lua;scheme=yaml://bench/scheme.yaml' \
	'lua+null://;code=file://bench/many.lua;scheme=yaml://bench/scheme.yaml' \
	'lua+null://;code=file://bench/static.lua;scheme=yaml://bench/scheme.yaml' \
	'lua+null://;code=file://bench/object.lua;scheme=yaml://bench/scheme.yaml;lua.message-mode=object'
