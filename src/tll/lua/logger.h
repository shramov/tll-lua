// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _TLL_LUA_LOGGER_H
#define _TLL_LUA_LOGGER_H

#include <tll/lua/luat.h>

#include <tll/logger.h>

namespace tll::lua {

struct Logger
{
	tll_logger_t * ptr = nullptr;
};

template <>
struct MetaT<Logger> : public MetaBase
{
	static constexpr std::string_view name = "tll_logger";
	static int index(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Logger>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "level") {
			lua_pushinteger(lua, self.ptr->level);
		} else if (key == "trace") {
			lua_pushcfunction(lua, log<TLL_LOGGER_TRACE>);
		} else if (key == "debug") {
			lua_pushcfunction(lua, log<TLL_LOGGER_DEBUG>);
		} else if (key == "info") {
			lua_pushcfunction(lua, log<TLL_LOGGER_INFO>);
		} else if (key == "warning" || key == "warn") {
			lua_pushcfunction(lua, log<TLL_LOGGER_WARNING>);
		} else if (key == "error") {
			lua_pushcfunction(lua, log<TLL_LOGGER_ERROR>);
		} else if (key == "critical") {
			lua_pushcfunction(lua, log<TLL_LOGGER_CRITICAL>);
		} else
			return luaL_error(lua, "Invalid Logger attribute '%s'", key.data());
		return 1;
	}

	static int gc(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Logger>(lua, 1);
		tll_logger_free(self.ptr);
		return 0;
	}

	template <tll_logger_level_t Level>
	static int log(lua_State *lua)
	{
		auto & self = luaT_checkuserdata<Logger>(lua, 1);
		auto msg = luaT_checkstringview(lua, 2);
		tll_logger_log(self.ptr, Level, msg.data(), msg.size());
		return 0;
	}
};

} // namespace tll::lua

#endif//_TLL_LUA_LOGGER_H
