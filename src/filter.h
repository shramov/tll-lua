/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_FILTER_H
#define _TLL_LUA_FILTER_H

#include "common.h"

#include <tll/channel/prefix.h>

class LuaFilter : public tll::lua::LuaCommon<LuaFilter, tll::channel::Prefix<LuaFilter>>
{
	using Base = tll::lua::LuaCommon<LuaFilter, tll::channel::Prefix<LuaFilter>>;

public:
	static constexpr std::string_view channel_protocol() { return "lua+"; }

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
