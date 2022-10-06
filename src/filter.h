/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_FILTER_H
#define _TLL_LUA_FILTER_H

#include "luat.h"
#include "reflection.h"

#include <tll/channel/prefix.h>

class LuaFilter : public tll::channel::Prefix<LuaFilter>
{
	using Base = tll::channel::Prefix<LuaFilter>;

	std::string _code;

	unique_lua_ptr_t _ptr = { nullptr, lua_close };
	lua_State * _lua = nullptr;
public:
	static constexpr std::string_view channel_protocol() { return "lua+"; }
	static constexpr auto process_policy() { return ProcessPolicy::Never; }

	int _init(const tll::Channel::Url &url, tll::Channel *master);

	int _open(const tll::ConstConfig &props);
	int _close(bool force);

	int _on_active()
	{
		_scheme.reset(tll_scheme_ref(_child->scheme()));
		return Base::_on_active();
	}

	int _on_closed()
	{
		_scheme.reset();
		return Base::_on_closed();
	}

	int _on_data(const tll_msg_t *msg);
};

#endif//_TLL_LUA_FILTER_H
