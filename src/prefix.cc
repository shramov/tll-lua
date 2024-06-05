/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "prefix.h"

#include <tll/scheme/merge.h>

using namespace tll::lua;

int LuaPrefix::_init(const tll::Channel::Url &url, tll::Channel * master)
{
	if (auto r = Base::_init(url, master); r)
		return r;

	auto reader = channel_props_reader(url);

	_fragile = reader.getT("fragile", false);

	if (!reader)
		return _log.fail(EINVAL, "Invalid url: {}", reader.error());

	auto child = _child->scheme(TLL_MESSAGE_CONTROL);
	if (child) {
		if (_scheme_control) {
			auto merged = tll::scheme::merge({_scheme_control.get(), child});
			if (!merged)
				return _log.fail(EINVAL, "Failed to merge control scheme with child: {}", merged.error());
			_scheme_control.reset(*merged);
		} else
			_scheme_control.reset(tll_scheme_ref(child));
	}

	return 0;
}

int LuaPrefix::_open(const tll::ConstConfig &props)
{
	if (auto r = _lua_open(); r)
		return r;

	lua_pushlightuserdata(_lua, this);
	lua_pushcclosure(_lua, _lua_post, 1);
	lua_setglobal(_lua, "tll_child_post");

	_on_data_name = "";
	lua_getglobal(_lua, "tll_on_data");
	if (lua_isfunction(_lua, -1))
		_on_data_name = "tll_on_data";
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "tll_on_post");
	_with_on_post = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "tll_filter");
	auto with_filter = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "tll_prefix_mode");
	auto mode = std::string(luaT_tostringview(_lua, -1));
	if (mode.empty()) {
		if (with_filter)
			_mode = Mode::Filter;
		else
			_mode = Mode::Normal;
	} else if (mode == "filter")
		_mode = Mode::Filter;
	else if (mode == "normal")
		_mode = Mode::Normal;
	else
		return _log.fail(EINVAL, "Unknown tll_prefix_mode: {}, has to be one of 'filter' or 'normal'", mode);
	lua_pop(_lua, 1);

	if (_mode == Mode::Filter && _on_data_name.empty()) {
		if (!with_filter)
			return _log.fail(EINVAL, "No 'tll_filter' function in filter mode");
		_on_data_name = "tll_filter";
	}

	luaT_push<tll::lua::Channel>(_lua, { self(), &_encoder });
	lua_setglobal(_lua, "tll_self");

	luaT_push<tll::lua::Channel>(_lua, { _child.get(), &_encoder });
	lua_setglobal(_lua, "tll_self_child");

	_open_cfg = props.copy();
	return Base::_open(props);
}

int LuaPrefix::_on_active()
{
	_scheme_child.reset(tll_scheme_ref(_child->scheme()));
	if (!_scheme)
		_scheme.reset(tll_scheme_ref(_child->scheme()));

	if (_scheme) {
		luaT_push(_lua, scheme::Scheme { _scheme.get() });
		lua_setglobal(_lua, "tll_self_scheme");
	}

	if (auto s = _child->scheme(); s) {
		luaT_push(_lua, scheme::Scheme { s });
		lua_setglobal(_lua, "tll_child_scheme");
	}

	if (auto r = _lua_on_open(_open_cfg); r)
		return r;

	if (state() != tll::state::Opening)
		return 0;
	return Base::_on_active();
}

int LuaPrefix::_on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, const tll::Channel * channel, std::string_view func, bool filter)
{
	auto ref = _lua.copy();
	lua_getglobal(ref, func.data());
	auto args = _lua_pushmsg(msg, scheme, channel, true);
	if (args < 0) {
		if (_fragile)
			state(tll::state::Error);
		return EINVAL;
	}
	//luaT_push(ref, msg);
	if (lua_pcall(ref, args, 1, 0)) {
		auto text = fmt::format("Lua function {} failed: {}\n  on", func, lua_tostring(ref, -1));
		lua_pop(ref, 1);
		tll_channel_log_msg(channel, _log.name(), tll::logger::Warning, _dump_error, msg, text.data(), text.size());
		if (_fragile)
			state(tll::state::Error);
		return EINVAL;
	}

	if (filter) {
		auto r = lua_toboolean(ref, -1);
		lua_pop(ref, 1);
		if (r)
			_callback_data(msg);
	} else
		lua_pop(ref, 1);
	return 0;
}
