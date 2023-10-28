/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "prefix.h"

int LuaPrefix::_open(const tll::ConstConfig &props)
{
	if (auto r = _lua_open(); r)
		return r;

	lua_pushlightuserdata(_lua, this);
	lua_pushcclosure(_lua, _lua_post, 1);
	lua_setglobal(_lua, "luatll_post");

	lua_getglobal(_lua, "luatll_on_data");
	_with_on_data = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "luatll_on_post");
	_with_on_post = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "luatll_open");
	if (lua_isfunction(_lua, -1)) {
		if (lua_pcall(_lua, 0, 0, 0))
			return _log.fail(EINVAL, "Lua open (luatll_open) failed: {}", lua_tostring(_lua, -1));
	}

	return Base::_open(props);
}

int LuaPrefix::_close(bool force)
{
	if (_lua) {
		lua_getglobal(_lua, "luatll_close");
		if (lua_isfunction(_lua, -1)) {
			if (lua_pcall(_lua, 0, 0, 0))
				_log.warning("Lua close (luatll_close) failed: {}", lua_tostring(_lua, -1));
		}
	}

	return Base::_close(force);
}

int LuaPrefix::_on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, std::string_view func)
{
	std::string_view name;
	lua_getglobal(_lua, func.data());
	lua_pushinteger(_lua, msg->seq);
	if (scheme) {
		auto message = scheme->lookup(msg->msgid);
		if (!message) {
			lua_settop(_lua, 0);
			return _log.fail(ENOENT, "Message {} not found", msg->msgid);
		}
		name = message->name;
		lua_pushstring(_lua, message->name);
		luaT_push(_lua, reflection::Message { message, tll::make_view(*msg) });
	} else {
		lua_pushnil(_lua);
		lua_pushlstring(_lua, (const char *) msg->data, msg->size);
	}
	lua_pushinteger(_lua, msg->msgid);
	lua_pushinteger(_lua, msg->addr.i64);
	lua_pushinteger(_lua, msg->time);
	//luaT_push(_lua, msg);
	if (lua_pcall(_lua, 6, 1, 0)) {
		_log.warning("Lua function {} failed for {}:{}: {}", func, name, msg->seq, lua_tostring(_lua, -1));
		lua_settop(_lua, 0);
		return EINVAL;
	}
	lua_settop(_lua, 0);
	return 0;
}
