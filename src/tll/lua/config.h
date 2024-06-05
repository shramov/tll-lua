// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _TLL_LUA_CONFIG_H
#define _TLL_LUA_CONFIG_H

#include <tll/lua/encoder.h>
#include <tll/lua/luat.h>
#include <tll/lua/scheme.h>

#include <tll/channel.h>

namespace tll::lua {

struct Config
{
	const tll_config_t * ptr = nullptr;
};

template <>
struct MetaT<Config> : public MetaBase
{
	static constexpr std::string_view name = "tll_config";
	static int index(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Config>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);
		int len = 0;

		if (key == "get")
			lua_pushcfunction(lua, get);
		else if (auto v = tll_config_get_copy(self.ptr, key.data(), key.size(), &len); v)
			luaT_pushstringview(lua, { v, (size_t) len });
		else
			lua_pushnil(lua);
		return 1;
	}

	static int get(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Config>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);
		int len = 0;
		if (auto v = tll_config_get_copy(self.ptr, key.data(), key.size(), &len); v)
			luaT_pushstringview(lua, { v, (size_t) len });
		else
			lua_pushnil(lua);
		return 1;
	}

	static int gc(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Config>(lua, 1);
		tll_config_unref(self.ptr);
		return 0;
	}

	static int browse_push(const char *key, int klen, const tll_config_t *value, void * data)
	{
		auto lua = (lua_State *) data;
		int len = 0;
		if (auto v = tll_config_get_copy(value, nullptr, 0, &len); v) {
			luaT_pushstringview(lua, std::string_view(key, klen));
			luaT_pushstringview(lua, std::string_view(v, len));
			lua_settable(lua, -3);
		}
		return 0;
	}

	static int pairs(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Config>(lua, 1);

		lua_getglobal(lua, "next");
		lua_newtable(lua);
		tll_config_browse(self.ptr, "**", -1, browse_push, lua);
		lua_pushnil(lua);
		return 3;
	}
};
} // namespace tll::lua

#endif//_TLL_LUA_CONFIG_H
