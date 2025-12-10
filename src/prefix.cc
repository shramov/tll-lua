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

	_scheme_control_init = std::move(_scheme_control);
	_scheme_control_child.reset(tll_scheme_ref(_child->scheme(TLL_MESSAGE_CONTROL)));

	auto reader = channel_props_reader(url);

	_fragile = reader.getT("fragile", true);

	if (!reader)
		return _log.fail(EINVAL, "Invalid url: {}", reader.error());

	if (auto r = _init_control(_scheme_control_child.get()); r)
		return r;

	return 0;
}

int LuaPrefix::_init_control(const tll::Scheme * child)
{
	if (child) {
		if (_scheme_control) {
			auto merged = tll::scheme::merge({_scheme_control_init.get(), child});
			if (!merged)
				return _log.fail(EINVAL, "Failed to merge control scheme with child: {}", merged.error());
			_scheme_control.reset(*merged);
		} else
			_scheme_control.reset(tll_scheme_ref(child));
	} else
		_scheme_control.reset(_scheme_control_init->ref());

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

	lua_getglobal(_lua, "tll_on_control");
	_with_on_control = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "tll_on_post");
	_with_on_post = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "tll_on_post_control");
	_with_on_post_control = lua_isfunction(_lua, -1);
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

	auto guard = state_guard();
	if (auto r = _lua_on_open(props); r)
		return r;

	if (guard) // Already opened
		return 0;
	return Base::_open(props);
}

int LuaPrefix::_on_active()
{
	_scheme_child.reset(tll_scheme_ref(_child->scheme()));
	if (!_scheme)
		_scheme.reset(tll_scheme_ref(_child->scheme()));

	if (auto control = _child->scheme(TLL_MESSAGE_CONTROL); control != _scheme_control_child.get()) {
		if (auto r = _init_control(control); r)
			return _log.fail(r, "Failed to initialize control scheme");
	}

	if (_scheme) {
		luaT_push(_lua, scheme::Scheme { _scheme.get() });
		lua_setglobal(_lua, "tll_self_scheme");
	}

	if (auto s = _child->scheme(); s) {
		luaT_push(_lua, scheme::Scheme { s });
		lua_setglobal(_lua, "tll_child_scheme");
	}

	lua_getglobal(_lua, "tll_on_active");
	if (lua_isfunction(_lua, -1)) {
		auto ref = _lua.copy();
		if (lua_pcall(ref, 0, 0, 0))
			return this->_log.fail(EINVAL, "Lua on active hook (tll_on_active) failed: {}", lua_tostring(ref, -1));
	}

	if (state() != tll::state::Opening)
		return 0;
	return Base::_on_active();
}

int LuaPrefix::_on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, const tll::Channel * channel, std::string_view func, bool filter)
{
	auto ref = _lua.copy();
	auto guard = StackGuard(ref);

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
		const auto level = _fragile ? tll::logger::Error : tll::logger::Warning;
		tll_channel_log_msg(channel, _log.name(), level, _dump_error, msg, text.data(), text.size());
		if (_fragile)
			state(tll::state::Error);
		return EINVAL;
	}

	if (filter) {
		auto r = lua_toboolean(ref, -1);
		if (r)
			_callback_data(msg);
	}
	return 0;
}
