/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "filter.h"

int LuaFilter::_init(const tll::Channel::Url &url, tll::Channel *master)
{
	auto reader = this->channel_props_reader(url);
	_code = reader.template getT<std::string>("code");
	if (!reader)
		return this->_log.fail(EINVAL, "Invalid url: {}", reader.error());

	return Base::_init(url, master);
}

int LuaFilter::_open(const tll::ConstConfig &props)
{
	unique_lua_ptr_t lua_ptr(luaL_newstate(), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return _log.fail(EINVAL, "Failed to create lua state");

	luaL_openlibs(lua);
	LuaT<reflection::Array>::init(lua);
	LuaT<reflection::Message>::init(lua);
	LuaT<reflection::Union>::init(lua);
	LuaT<reflection::Bits>::init(lua);

	if (luaL_loadfile(lua, _code.c_str()))
		return this->_log.fail(EINVAL, "Failed to load file '{}': {}", _code, lua_tostring(lua, -1));

	if (lua_pcall(lua, 0, 0, 0))
		return this->_log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));

	lua_getglobal(lua, "luatll_filter");
	if (!lua_isfunction(lua, -1))
		return _log.fail(EINVAL, "Function luatll_filter not defined");
	lua_pop(lua, 1);

	lua_getglobal(lua, "luatll_open");
	if (lua_isfunction(lua, -1)) {
		if (lua_pcall(lua, 0, 0, 0))
			return _log.fail(EINVAL, "Lua open (luatll_open) failed: {}", lua_tostring(lua, -1));
	}


	_ptr.reset(lua_ptr.release());
	_lua = _ptr.get();

	return Base::_open(props);
}

int LuaFilter::_close(bool force)
{
	if (_lua) {
		lua_getglobal(_lua, "luatll_close");
		if (lua_isfunction(_lua, -1)) {
			if (lua_pcall(_lua, 0, 0, 0))
				_log.warning("Lua close (luatll_close) failed: {}", lua_tostring(_lua, -1));
		}
	}

	_ptr.reset();
	_lua = nullptr;
	return Base::_close(force);
}

int LuaFilter::_on_data(const tll_msg_t *msg)
{
	tll::scheme::Message * message;
	for (message = _scheme->messages; message; message = message->next) {
		if (message->msgid == msg->msgid)
			break;
	}
	if (!message)
		return _log.fail(ENOENT, "Message {} not found", msg->msgid);

	lua_getglobal(_lua, "luatll_filter");
	lua_pushstring(_lua, message->name);
	lua_pushinteger(_lua, msg->seq);
	luaT_push(_lua, reflection::Message { message, tll::make_view(*msg) });
	if (lua_pcall(_lua, 3, 1, 0)) {
		_log.warning("Lua filter failed for {}:{}: {}", message->name, msg->seq, lua_tostring(_lua, -1));
		lua_pop(_lua, 1);
		return EINVAL;
	}
	auto r = lua_toboolean(_lua, -1);
	lua_pop(_lua, 1);
	if (r) {
		_callback_data(msg);
	}
	return 0;
}
