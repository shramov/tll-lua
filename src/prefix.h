/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_PREFIX_H
#define _TLL_LUA_PREFIX_H

#include "tll/lua/base.h"

#include <tll/channel/prefix.h>

class LuaPrefix : public tll::lua::LuaBase<LuaPrefix, tll::channel::Prefix<LuaPrefix>>
{
	using Base = tll::lua::LuaBase<LuaPrefix, tll::channel::Prefix<LuaPrefix>>;

	std::vector<char> _buf;
	tll_msg_t _msg;

	tll::scheme::ConstSchemePtr _scheme_child;

	bool _with_on_post = false;
	std::string _on_data_name;

	enum class Mode { Normal, Filter };
	Mode _mode = Mode::Normal;

public:
	static constexpr std::string_view channel_protocol() { return "lua+"; }
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
		if (_on_data_name.empty())
			return Base::_on_data(msg);
		_on_msg(msg, _scheme_child.get(), _on_data_name, _mode == Mode::Filter);
		return 0;
	}

	int _post(const tll_msg_t *msg, int flags)
	{
		if (!_with_on_post)
			return Base::_post(msg, flags);
		if (_on_msg(msg, _scheme.get(), "tll_on_post"))
			return EINVAL;
		return 0;
	}

	static int _lua_post(lua_State * lua)
	{
		if (auto self = _lua_self(lua, 1); self) {
			auto msg = self->_encoder.encode_stack(lua, self->_scheme_child.get(), self->_child.get(), 0);
			if (!msg) {
				self->_log.error("Failed to convert messge: {}", self->_encoder.error);
				return luaL_error(lua, "Failed to convert message");
			}
			if (auto r = self->_child->post(msg); r)
				return luaL_error(lua, "Failed to post: %d", r);
			return 0;
		}
		return luaL_error(lua, "Non-userdata value in upvalue");
	}

	int _on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, std::string_view func, bool filter = false);
};

#endif//_TLL_LUA_PREFIX_H
