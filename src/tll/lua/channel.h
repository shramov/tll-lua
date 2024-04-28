// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _TLL_LUA_CHANNEL_H
#define _TLL_LUA_CHANNEL_H

#include <tll/lua/encoder.h>
#include <tll/lua/luat.h>
#include <tll/lua/scheme.h>

#include <tll/channel.h>

namespace tll::lua {

struct Channel
{
	tll::Channel * ptr = nullptr;
	tll::lua::Encoder * encoder = nullptr;
};

struct Context
{
	tll_channel_context_t * ptr = nullptr;
	tll::lua::Encoder * encoder = nullptr;
};

template <>
struct MetaT<Channel> : public MetaBase
{
	static constexpr std::string_view name = "tll_channel";
	static int index(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Channel>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "name") {
			lua_pushstring(lua, self.ptr->name());
		} else if (key == "post") {
			lua_pushcfunction(lua, post);
		} else if (key == "scheme") {
			lua_pushcfunction(lua, scheme);
		} else if (key == "context") {
			luaT_push<Context>(lua, { self.ptr->context(), self.encoder });
		} else
			return luaL_error(lua, "Invalid Channel attribute '%s'", key.data());
		return 1;
	}

	static int scheme(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Channel>(lua, 1);
		std::string_view mstr = "data";
		if (lua_gettop(lua) >= 2)
			mstr = luaT_checkstringview(lua, 2);
		auto mode = TLL_MESSAGE_DATA;
		if (mstr == "data") {
		} else if (mstr == "control")
			mode = TLL_MESSAGE_CONTROL;
		else
			return luaL_error(lua, "Invalid scheme mode: '%s', need one of 'data' or 'control'", mstr.data());
		if (auto s = self.ptr->scheme(mode); s)
			luaT_push(lua, scheme::Scheme { s });
		else
			lua_pushnil(lua);
		return 1;
	}

	static int post(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Channel>(lua, 1);
		auto msg = self.encoder->encode_stack(lua, self.ptr->scheme(), self.ptr, 1);
		if (!msg)
			return luaL_error(lua, "Failed to convert message: %s", self.encoder->error.c_str());
		if (auto r = self.ptr->post(msg); r)
			return luaL_error(lua, "Failed to post: %d", r);
		return 0;
	}
};

template <>
struct MetaT<Context> : public MetaBase
{
	static constexpr std::string_view name = "tll_channel_context";
	static int index(lua_State* lua)
	{
		auto key = luaT_checkstringview(lua, 2);

		if (key == "get") {
			lua_pushcfunction(lua, get);
		} else
			return luaL_error(lua, "Invalid Context attribute '%s'", key.data());

		return 1;
	}

	static int get(lua_State* lua)
	{
		auto & self = luaT_checkuserdata<Context>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		tll::channel::Context ctx(self.ptr);
		auto c = ctx.get(key);
		if (c) {
			luaT_push<Channel>(lua, {c, self.encoder});
		} else
			lua_pushnil(lua);
		return 1;
	}
};

} // namespace tll::lua

#endif//_TLL_LUA_CHANNEL_H
