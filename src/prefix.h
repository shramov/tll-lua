/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_PREFIX_H
#define _TLL_LUA_PREFIX_H

#include "common.h"

#include <tll/channel/prefix.h>

class LuaPrefix : public tll::lua::LuaCommon<LuaPrefix, tll::channel::Prefix<LuaPrefix>>
{
	using Base = tll::lua::LuaCommon<LuaPrefix, tll::channel::Prefix<LuaPrefix>>;

	std::vector<char> _buf;
	tll_msg_t _msg;

	tll::scheme::ConstSchemePtr _scheme_child;

	bool _with_on_post = false;
	bool _with_on_data = false;

public:
	static constexpr std::string_view channel_protocol() { return "lua-prefix+"; }
	static constexpr auto scheme_policy() { return Base::SchemePolicy::Normal; }

	const tll::Scheme * scheme(int type) const
	{
		if (type == TLL_MESSAGE_DATA)
			return _scheme.get();
		return Base::scheme(type);
	}

	int _open(const tll::ConstConfig &props);
	int _close(bool force);

	int _on_active()
	{
		_scheme_child.reset(tll_scheme_ref(_child->scheme()));
		if (!_scheme)
			_scheme.reset(tll_scheme_ref(_child->scheme()));
		return Base::_on_active();
	}

	int _on_closed()
	{
		_scheme_child.reset();
		return Base::_on_closed();
	}

	int _on_data(const tll_msg_t *msg)
	{
		if (!_with_on_data)
			return Base::_on_data(msg);
		_on_msg(msg, _scheme_child.get(), "luatll_on_data");
		return 0;
	}

	int _post(const tll_msg_t *msg, int flags)
	{
		if (!_with_on_post)
			return Base::_post(msg, flags);
		if (_on_msg(msg, _scheme.get(), "luatll_on_post"))
			return EINVAL;
		return 0;
	}

	static LuaPrefix * _self(lua_State * lua)
	{
		lua_getglobal(lua, "luatll_self");
		if (!lua_isuserdata(lua, -1)) {
			lua_pop(lua, 1);
			return nullptr;
		}
		auto ptr = (LuaPrefix *) lua_topointer(lua, -1);
		lua_pop(lua, 1);
		return ptr;
	}

	static int _lua_post(lua_State * lua)
	{
		if (auto self = _self(lua); self) {
			auto msg = self->_lua_msg(lua, self->_scheme_child.get());
			if (!msg)
				return luaL_error(lua, "Failed to convert message");
			if (auto r = self->_child->post(msg); r)
				return luaL_error(lua, "Failed to post: %d", r);
			return 0;
		}
		return luaL_error(lua, "Non-userdata value in luatll_self");
	}

	static int _lua_callback(lua_State * lua)
	{
		if (auto self = _self(lua); self) {
			auto msg = self->_lua_msg(lua, self->_scheme.get());
			if (!msg)
				return luaL_error(lua, "Failed to convert message");
			self->_callback(msg);
			return 0;
		}
		return luaL_error(lua, "Non-userdata value in luatll_self");
	}

	tll_msg_t * _lua_msg(lua_State * lua, const tll::Scheme * scheme);
	int _on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, std::string_view func);

	template <typename Buf>
	int _fill(const tll::scheme::Message * message, Buf view, lua_State * lua, int index);

	template <typename Buf>
	int _fill(const tll::scheme::Field * field, Buf view, lua_State * lua, int index);

	template <typename T, typename Buf>
	int _fill_numeric(const tll::scheme::Field * field, Buf view, lua_State * lua);
};

#endif//_TLL_LUA_PREFIX_H
