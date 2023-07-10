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
	lua_setglobal(_lua, "luatll_self");

	lua_pushcfunction(_lua, _lua_post);
	lua_setglobal(_lua, "luatll_post");

	lua_pushcfunction(_lua, _lua_callback);
	lua_setglobal(_lua, "luatll_callback");

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

tll_msg_t * LuaPrefix::_lua_msg(lua_State * lua, const tll::Scheme * scheme)
{
	auto args = lua_gettop(lua);
	if (args < 3)
		return _log.fail(nullptr, "Too small number of arguments: {}", args);
	_msg = { TLL_MESSAGE_DATA };

	if (lua_isinteger(lua, 1))
		_msg.seq = lua_tointeger(lua, 1);

	const tll::scheme::Message * message = nullptr;
	if (lua_isnil(lua, 2)) {
		// Pass
	} else if (lua_isinteger(lua, 2)) {
		_msg.msgid = lua_tointeger(lua, 2);
		if (scheme) {
			message = scheme->lookup(_msg.msgid);
			if (!message)
				return _log.fail(nullptr, "Message '{}' not found in scheme", name);
		}
	} else if (lua_isstring(lua, 1)) {
		if (!scheme)
			return _log.fail(nullptr, "Message name '{}' without scheme", name);
		auto name = luaT_tostringview(lua, 2);
		message = scheme->lookup(name);
		if (!message)
			return _log.fail(nullptr, "Message '{}' not found in scheme", name);
		_msg.msgid = message->msgid;
	} else
		return _log.fail(nullptr, "Invalid message name/id argument");

	if (args > 4 && lua_isinteger(lua, 4))
		_msg.addr.i64 = lua_tointeger(lua, 4);

	if (lua_isstring(lua, 3)) {
		auto data = luaT_tostringview(lua, 3);
		_msg.data = data.data();
		_msg.size = data.size();
		return &_msg;
	}

	if (auto data = luaT_testuserdata<reflection::Message>(lua, 3); data) {
		if (!message || message == data->message) {
			_msg.msgid = data->message->msgid;
			_msg.data = data->data.data();
			_msg.size = data->data.size();
			return &_msg;
		}
	} else if (!lua_istable(lua, 3)) {
		return _log.fail(nullptr, "Invalid type of data: allowed string, table and Message");
	}

	_buf.resize(0);
	_buf.resize(message->size);
	auto view = tll::make_view(_buf);

	if (_encoder.encode(message, view, lua, 3))
		return _log.fail(nullptr, "Failed to encode Lua message at {}: {}", _encoder.format_stack(), _encoder.error);

	_msg.data = _buf.data();
	_msg.size = _buf.size();

	return &_msg;
}
