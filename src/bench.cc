/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "luat.h"
#include "msg.h"

#include <chrono>
#include <tll/logger.h>
#include <tll/util/bench.h>

static constexpr auto count = 1000000u;

using namespace std::chrono;

static constexpr std::string_view zabbix_lua = R"(
-- Frame size
frame_size = 13

function frame_pack(msg)
	return string.pack("c5 I8", "ZBXD\x01", msg.size)
end

function frame_unpack(frame, msg)
	prefix, size = string.unpack("c5 I8", frame)
	msg.size = size
	return frame_size
end
)";

static constexpr std::string_view call_lua = R"(
function call_meta(msg)
	return 10
end

function call_meta_get(msg)
	return msg.size
end

function call0()
	return 10
end

function call1(a0)
	return a0
end

function call5(a0, a1, a2, a3, a4)
	return a2
end

function call10(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9)
	return a5
end
)";

std::string_view pack(lua_State * lua, const tll_msg_t *msg)
{
	lua_getglobal(lua, "frame_pack");
	luaT_push(lua, msg);
	if (lua_pcall(lua, 1, 1, 0))
		return { nullptr, 0 };
	auto s = luaT_tostringview(lua, -1);
	lua_pop(lua, 1);
	return s;
}

int unpack(lua_State * lua, std::string_view frame, tll_msg_t *msg)
{
	lua_getglobal(lua, "frame_unpack");
	lua_pushlstring(lua, frame.data(), frame.size());
	luaT_push(lua, msg);
	if (lua_pcall(lua, 2, 1, 0))
		return EINVAL;
	lua_pop(lua, 1);
	return 0;
}

lua_State * init(std::string_view code)
{
	tll::Logger log("bench");
	std::unique_ptr<lua_State, decltype(&lua_close)> lua_ptr(luaL_newstate(), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return log.fail(nullptr, "Failed to create lua state");

	luaL_openlibs(lua);
	LuaT<tll_msg_t *>::init(lua);
	LuaT<const tll_msg_t *>::init(lua);

	if (luaL_loadstring(lua, code.data()))
		return log.fail(nullptr, "Failed to load code {}:\n{}", lua_tostring(lua, -1), code);

	if (lua_pcall(lua, 0, 0, 0))
		return log.fail(nullptr, "Failed to init globals: {}", lua_tostring(lua, -1));
	return lua_ptr.release();
}

int pack_unpack(lua_State *lua, tll_msg_t &msg)
{
	auto i = ++msg.size;
	auto frame = pack(lua, &msg);
	msg.size = 0;
	unpack(lua, frame, &msg);
	return msg.size - i;
}

int bench_frame(tll::Logger &log)
{
	std::unique_ptr<lua_State, decltype(&lua_close)> lua_ptr(init(zabbix_lua), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return log.fail(EINVAL, "Failed to init lua state");

	lua_getglobal(lua, "frame_size");
	auto size = lua_tointeger(lua, -1);
	if (size <= 0 || size > 64)
		return log.fail(EINVAL, "Invalid frame size: {}", size);
	log.info("Lua frame size: {}", size);

	tll_msg_t msg = {};

	tll::bench::timeit(count, "frame", pack_unpack, lua, msg);

	msg.size = 100;
	auto delta = pack_unpack(lua, msg);
	if (delta)
		return log.fail(EINVAL, "Pack/unpack calls does not match: non zero delta {}", delta);

	return 0;
}

int call_meta(lua_State *lua, std::string_view name, tll_msg_t &msg)
{
	lua_getglobal(lua, name.data());
	luaT_push(lua, &msg);
	if (lua_pcall(lua, 1, 1, 0))
		return EINVAL;
	auto r = lua_tointeger(lua, -1);
	lua_pop(lua, 1);
	return msg.size - r;
}

constexpr const char * call_name(size_t s)
{
	switch (s) {
	case 0: return "call0";
	case 1: return "call1";
	case 5: return "call5";
	case 10: return "call10";
	default:
		break;
	}
	return "unknown";
}

template <size_t Size>
int callT(lua_State *lua, int x)
{
	lua_getglobal(lua, call_name(Size));
	x++;
	for (auto i = 0u; i < Size; i++)
		lua_pushinteger(lua, x);
	if (lua_pcall(lua, Size, 1, 0))
		return EINVAL;
	auto r = lua_tointeger(lua, -1);
	lua_pop(lua, 1);
	return x - r;
}

int bench_call(tll::Logger &log)
{
	std::unique_ptr<lua_State, decltype(&lua_close)> lua_ptr(init(call_lua), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return log.fail(EINVAL, "Failed to init lua state");

	tll_msg_t msg = {};
	int x = 0;

	tll::bench::timeit(count, "call0", callT<0>, lua, x); x = 0;
	tll::bench::timeit(count, "call1", callT<1>, lua, x); x = 0;
	tll::bench::timeit(count, "call5", callT<5>, lua, x); x = 0;
	tll::bench::timeit(count, "call10", callT<10>, lua, x); x = 0;
	tll::bench::timeit(count, "meta", call_meta, lua, "call_meta", msg);
	tll::bench::timeit(count, "meta.get", call_meta, lua, "call_meta_get", msg);

	return 0;
}

int main()
{
	tll::Logger log("bench");

	tll::bench::prewarm(100ms);
	bench_frame(log);
	tll::bench::prewarm(100ms);
	bench_call(log);
	return 0;
}
