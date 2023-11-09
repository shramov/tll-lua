/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "tll/lua/luat.h"
#include "tll/lua/message.h"

#include <chrono>
#include <tll/logger.h>
#include <tll/util/bench.h>

static constexpr auto count = 1000000u;

using namespace std::chrono;
using namespace tll::lua;

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

function call_meta_get_sum(msg)
	return msg.size + msg.size
end

function call_meta_get_sum_var(msg)
	tmp = msg.size
	return tmp + tmp
end

function call_meta_get_sum_func(msg)
	return call_sum(msg.size)
end

function call_sum(a0)
	return a0 + a0
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

function call_call1(a0)
	return call1(a0)
end

function call_call_call1(a0)
	return call_call1(a0)
end

function call_cclosure(a0)
	return cclosure(a0)
end

function call_cglobal(a0)
	return cglobal(a0)
end

table_global = { counter = 10 }
function table_index()
	return table_global.counter
end

function mtable_index()
	return mtable_global.counter
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

template <size_t Size>
int callT(lua_State *lua, int x, std::string_view name)
{
	lua_getglobal(lua, name.data());
	x++;
	for (auto i = 0u; i < Size; i++)
		lua_pushinteger(lua, x);
	if (lua_pcall(lua, Size, 1, 0)) {
		fmt::print("call {} failed: {}\n", name, lua_tostring(lua, -1));
		return EINVAL;
	}
	auto r = lua_tointeger(lua, -1);
	lua_pop(lua, 1);
	return x - r;
}

struct Counter
{
	unsigned counter = 0;

	int call(lua_State * lua)
	{
		lua_pushinteger(lua, ++counter);
		return 1;
	}
};

template <>
struct tll::lua::MetaT<Counter> : public MetaBase
{
	static constexpr std::string_view name = "counter";
	static int index(lua_State* lua)
	{
		auto & r = * (Counter *) lua_touserdata(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "counter") {
			lua_pushnumber(lua, r.counter);
			return 1;
		}

		return luaL_error(lua, "Key '%s' not supported", key.data());
	}
};

/*
 * Test different ways of getting pointer from upvalue
 */
int cclosure_is(lua_State *lua)
{
	if (!lua_isuserdata(lua, lua_upvalueindex(1)))
		return luaL_error(lua, "Non-userdata upvalue");
	auto counter = (Counter *) lua_topointer(lua, lua_upvalueindex(1));
	return counter->call(lua);
}

int cclosure_to(lua_State *lua)
{
	auto counter = (Counter *) lua_touserdata(lua, lua_upvalueindex(1));
	if (!counter)
		return luaL_error(lua, "Non-userdata upvalue");
	return counter->call(lua);
}

int cclosure_cast(lua_State *lua)
{
	auto counter = (Counter *) lua_topointer(lua, lua_upvalueindex(1));
	return counter->call(lua);
}

int cclosure_meta(lua_State *lua)
{
	auto & counter = luaT_checkuserdata<Counter>(lua, lua_upvalueindex(1));
	return counter.call(lua);
}

int cglobal(lua_State *lua)
{
	lua_getglobal(lua, "global_counter");
	if (!lua_isuserdata(lua, -1)) {
		lua_pop(lua, 1);
		return luaL_error(lua, "Non-userdata global");
	}
	auto counter = (Counter *) lua_topointer(lua, -1);
	lua_pop(lua, 1);

	return counter->call(lua);
}

int bench_call(tll::Logger &log)
{
	std::unique_ptr<lua_State, decltype(&lua_close)> lua_ptr(init(call_lua), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return log.fail(EINVAL, "Failed to init lua state");

	LuaT<Counter>::init(lua);

	tll_msg_t msg = {};
	int x = 0;

	Counter counter;
	lua_pushlightuserdata(lua, &counter);
	lua_setglobal(lua, "global_counter");

	lua_pushcfunction(lua, cglobal);
	lua_setglobal(lua, "cglobal");

	lua_pushlightuserdata(lua, &counter);
	lua_pushcclosure(lua, cclosure_is, 1);
	lua_setglobal(lua, "cclosure_is");

	lua_pushlightuserdata(lua, &counter);
	lua_pushcclosure(lua, cclosure_to, 1);
	lua_setglobal(lua, "cclosure_to");

	lua_pushlightuserdata(lua, &counter);
	lua_pushcclosure(lua, cclosure_cast, 1);
	lua_setglobal(lua, "cclosure_cast");

	luaT_push(lua, counter);
	lua_pushcclosure(lua, cclosure_meta, 1);
	lua_setglobal(lua, "cclosure_meta");

	luaT_push(lua, counter);
	lua_setglobal(lua, "mtable_global");

	tll::bench::timeit(count, "call0", callT<0>, lua, x, "call0"); x = 0;
	tll::bench::timeit(count, "call1", callT<1>, lua, x, "call1"); x = 0;
	tll::bench::timeit(count, "call5", callT<5>, lua, x, "call5"); x = 0;
	tll::bench::timeit(count, "call10", callT<10>, lua, x, "call10"); x = 0;
	tll::bench::timeit(count, "call1", callT<1>, lua, x, "call1"); x = 0;
	tll::bench::timeit(count, "call(call1)", callT<1>, lua, x, "call_call1"); x = 0;
	tll::bench::timeit(count, "call(call(call1))", callT<1>, lua, x, "call_call_call1"); x = 0;
	tll::bench::timeit(count, "call10", callT<10>, lua, x, "call10"); x = 0;
	tll::bench::timeit(count, "meta", call_meta, lua, "call_meta", msg);
	tll::bench::timeit(count, "meta.get", call_meta, lua, "call_meta_get", msg);
	tll::bench::timeit(count, "meta.get + meta.get", call_meta, lua, "call_meta_get_sum", msg);
	tll::bench::timeit(count, "sum(meta.get)", call_meta, lua, "call_meta_get_sum_func", msg);

	tll::bench::timeit(count, "global userdata", callT<0>, lua, x, "cglobal"); x = 0;
	tll::bench::timeit(count, "upvalue isuserdata", callT<0>, lua, x, "cclosure_is"); x = 0;
	tll::bench::timeit(count, "upvalue touserdata", callT<0>, lua, x, "cclosure_to"); x = 0;
	tll::bench::timeit(count, "upvalue cast", callT<0>, lua, x, "cclosure_cast"); x = 0;
	tll::bench::timeit(count, "upvalue metacast", callT<0>, lua, x, "cclosure_meta"); x = 0;

	tll::bench::timeit(count, "table index", callT<0>, lua, x, "table_index"); x = 0;
	tll::bench::timeit(count, "metatable index", callT<0>, lua, x, "mtable_index"); x = 0;

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
