/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_BASE_H
#define _TLL_LUA_BASE_H

#include "tll/lua/encoder.h"
#include "tll/lua/luat.h"
#include "tll/lua/reflection.h"

#include <tll/channel/base.h>

namespace tll::lua {

template <typename T, typename B = tll::channel::Base<T>>
class LuaBase : public B
{
 protected:
	using Base = B;

	std::string _code;
	std::string _extra_path;

	unique_lua_ptr_t _lua_ptr = { nullptr, lua_close };
	lua_State * _lua = nullptr;

	tll::lua::Encoder _encoder;
 public:
	int _init(const tll::Channel::Url &url, tll::Channel *master)
	{
		auto reader = this->channel_props_reader(url);
		_code = reader.template getT<std::string>("code");
		_extra_path = reader.template getT<std::string>("path", "");
		if (!reader)
			return this->_log.fail(EINVAL, "Invalid url: {}", reader.error());

		if (_extra_path.size())
			_extra_path += ";";

		for (auto [k, c] : url.browse("lua.path.**")) {
			auto v = c.get();
			if (!v || v->empty()) continue;
			_extra_path += std::string(*v) + ";";
		}

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

		if (_extra_path.size()) {
			lua_getglobal(lua, "package");
			luaT_pushstringview(lua, "path");
			lua_gettable(lua, -2);
			auto path = std::string(luaT_tostringview(lua, -1));
			this->_log.info("Extend current path: {} with {}", path, _extra_path);
			lua_pop(lua, 1);
			path = _extra_path + path;
			luaT_pushstringview(lua, "path");
			luaT_pushstringview(lua, path);
			lua_settable(lua, -3);
			lua_pop(lua, 1);
		}

		if (_code.substr(0, 7) == "file://") {
			if (luaL_loadfile(lua, _code.substr(7).c_str()))
				return this->_log.fail(EINVAL, "Failed to load file '{}': {}", _code, lua_tostring(lua, -1));
		} else {
			if (luaL_loadstring(lua, _code.c_str()))
				return this->_log.fail(EINVAL, "Failed to load source code '{}':\n{}", lua_tostring(lua, -1), _code);
		}

		if (lua_pcall(lua, 0, LUA_MULTRET, 0))
			return this->_log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));

		lua_pushcfunction(lua, MetaT<reflection::Message>::copy);
		lua_setglobal(lua, "tll_msg_copy");

		lua_pushlightuserdata(lua, this->channelT());
		lua_pushcclosure(lua, _lua_callback, 1);
		lua_setglobal(lua, "tll_callback");

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

	void _lua_pushconfig(lua_State * lua, const tll::ConstConfig &cfg)
	{
		lua_newtable(lua);
		for (auto &[k, c] : cfg.browse("**")) {
			auto v = c.get();
			if (!v) continue;
			luaT_pushstringview(lua, k);
			luaT_pushstringview(lua, *v);
			lua_settable(lua, -3);
		}
	}

	static T * _lua_self(lua_State * lua, int index)
	{
		return (T *) lua_touserdata(lua, lua_upvalueindex(index));
	}

	static int _lua_callback(lua_State * lua)
	{
		if (auto self = _lua_self(lua, 1); self) {
			auto msg = self->_encoder.encode_stack(lua, self->_scheme.get(), 0);
			if (!msg) {
				self->_log.error("Failed to convert messge: {}", self->_encoder.error);
				return luaL_error(lua, "Failed to convert message");
			}
			self->_callback(msg);
			return 0;
		}
		return luaL_error(lua, "Non-userdata value in upvalue");
	}
};

} // namespace tll::lua

#endif//_TLL_LUA_BASE_H
