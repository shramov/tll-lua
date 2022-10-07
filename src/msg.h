/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_MSG_H
#define _TLL_LUA_MSG_H

#include "luat.h"
#include <tll/channel.h>

template <>
struct MetaT<const tll_msg_t *> : public MetaBase
{
	static constexpr std::string_view name = "const_tll_msg_t_meta";
	static int index(lua_State* lua) { return _index(lua, name); }

	static int _index(lua_State* lua, std::string_view tag)
	{
		auto msg = luaT_checkuserdata<const tll_msg_t *>(lua, 1, tag);
		auto key = luaT_checkstringview(lua, 2);
		if (key == "msgid")
			lua_pushinteger(lua, msg->msgid);
		else if (key == "seq")
			lua_pushinteger(lua, msg->seq);
		else if (key == "size")
			lua_pushinteger(lua, msg->size);
		else if (key == "data")
			lua_pushlstring(lua, (const char *) msg->data, msg->size);
		else if (key == "addr")
			lua_pushinteger(lua, msg->addr.i64);
		else
			luaL_argerror(lua, 2, key.data());
		return 1;
	}
};

template <>
struct MetaT<tll_msg_t *> : public MetaT<const tll_msg_t *>
{
	static constexpr std::string_view name = "tll_msg_t_meta";
	static int index(lua_State* lua) { return _index(lua, name); }

	static int newindex(lua_State* lua)
	{
		auto msg = luaT_checkuserdata<tll_msg_t *>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);
		auto value = luaL_checkinteger(lua, 3);
		if (key == "msgid")
			msg->msgid = value;
		else if (key == "seq")
			msg->seq = value;
		else if (key == "size")
			msg->size = value;
		else
			luaL_argerror(lua, 2, key.data());
		return 0;
	}
};

#endif//_TLL_LUA_MSG_H
