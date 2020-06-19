/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "luat.h"
#include "msg.h"

#include <chrono>
#include <fmt/chrono.h>
#include <tll/logger.h>

using namespace std::chrono;
template <typename Res>
using fduration = duration<double, Res>;

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

int main()
{
	tll::Logger log("bench");
	std::unique_ptr<lua_State, decltype(&lua_close)> lua_ptr(luaL_newstate(), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return log.fail(EINVAL, "Failed to create lua state");

	luaL_openlibs(lua);
	LuaT<tll_msg_t *>::init(lua);
	LuaT<const tll_msg_t *>::init(lua);

	if (luaL_loadfile(lua, "zabbix.lua"))
		return log.fail(EINVAL, "Failed to load file '{}': {}", "zabbix.lua", lua_tostring(lua, -1));

	if (lua_pcall(lua, 0, 0, 0))
		return log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));

	lua_getglobal(lua, "frame_size");
	auto size = lua_tointeger(lua, -1);
	if (size <= 0 || size > 64)
		return log.fail(EINVAL, "Invalid frame size: {}", size);
	log.info("Lua frame size: {}", size);

	tll_msg_t msg = {};

	auto start = system_clock::now();

	constexpr auto count = 1000000u;
	int delta = 0;
	for (auto i = 0u; i < count; i++) {
		msg.size = i;
		auto frame = pack(lua, &msg);
		msg.size = 0;
		unpack(lua, frame, &msg);
		delta += msg.size - i;
	}

	auto dt = system_clock::now() - start;

	log.info("Time {}/{}, {}", duration_cast<fduration<std::milli>>(dt), count, duration_cast<fduration<std::micro>>(dt) / count);
	if (delta)
		return log.fail(EINVAL, "Pack/unpack calls does not match: non zero delta {}", delta);
	return 0;
}
