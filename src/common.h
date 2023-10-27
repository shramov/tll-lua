/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _COMMON_H
#define _COMMON_H

#include "encoder.h"
#include "luat.h"
#include "reflection.h"

#include <tll/channel/base.h>

namespace tll::lua {

template <typename T, typename B = tll::channel::Base<T>>
class LuaCommon : public B
{
 protected:
	using Base = B;

	std::string _code;

	unique_lua_ptr_t _lua_ptr = { nullptr, lua_close };
	lua_State * _lua = nullptr;

	tll::lua::Encoder _encoder;
 public:
	int _init(const tll::Channel::Url &url, tll::Channel *master)
	{
		auto reader = this->channel_props_reader(url);
		_code = reader.template getT<std::string>("code");
		if (!reader)
			return this->_log.fail(EINVAL, "Invalid url: {}", reader.error());

		return Base::_init(url, master);
	}

	int _lua_open()
	{
		unique_lua_ptr_t lua_ptr(luaL_newstate(), lua_close);
		auto lua = lua_ptr.get();
		if (!lua)
			return this->_log.fail(EINVAL, "Failed to create lua state");

		luaL_openlibs(lua);
		LuaT<reflection::Array>::init(lua);
		LuaT<reflection::Message>::init(lua);
		LuaT<reflection::Message::Iterator>::init(lua);
		LuaT<reflection::Union>::init(lua);
		LuaT<reflection::Bits>::init(lua);

		if (_code.substr(0, 7) == "file://") {
			if (luaL_loadfile(lua, _code.substr(7).c_str()))
				return this->_log.fail(EINVAL, "Failed to load file '{}': {}", _code, lua_tostring(lua, -1));
		} else {
			if (luaL_loadstring(lua, _code.c_str()))
				return this->_log.fail(EINVAL, "Failed to load source code '{}':\n{}", lua_tostring(lua, -1), _code);
		}

		if (lua_pcall(lua, 0, 0, 0))
			return this->_log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));

		lua_pushcfunction(lua, MetaT<reflection::Message>::copy);
		lua_setglobal(lua, "luatll_msg_copy");

		lua_pushlightuserdata(lua, this->channelT());
		lua_setglobal(lua, "luatll_self");

		lua_pushcfunction(lua, _lua_callback);
		lua_setglobal(lua, "luatll_callback");

		_lua_ptr = std::move(lua_ptr);
		_lua = _lua_ptr.get();

		return 0;
	}

	int _close(bool force = false)
	{
		_lua = nullptr;
		_lua_ptr.reset();
		return Base::_close(force);
	}

	static T * _lua_self(lua_State * lua)
	{
		lua_getglobal(lua, "luatll_self");
		if (!lua_isuserdata(lua, -1)) {
			lua_pop(lua, 1);
			return nullptr;
		}
		auto ptr = (T *) lua_topointer(lua, -1);
		lua_pop(lua, 1);
		return ptr;
	}

	static int _lua_callback(lua_State * lua)
	{
		if (auto self = _lua_self(lua); self) {
			auto msg = self->_encoder.encode_stack(lua, self->_scheme.get(), 0);
			if (!msg) {
				self->_log.error("Failed to convert messge: {}", self->_encoder.error);
				return luaL_error(lua, "Failed to convert message");
			}
			self->_callback(msg);
			return 0;
		}
		return luaL_error(lua, "Non-userdata value in luatll_self");
	}
};

} // namespace tll::lua

#endif//_COMMON_H
