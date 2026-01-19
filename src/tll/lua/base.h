/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_BASE_H
#define _TLL_LUA_BASE_H

#include "tll/lua/channel.h"
#include "tll/lua/config.h"
#include "tll/lua/encoder.h"
#include "tll/lua/logger.h"
#include "tll/lua/luat.h"
#include "tll/lua/reflection.h"
#include "tll/lua/scheme.h"
#include "tll/lua/time.h"

#include <tll/channel/base.h>

namespace tll::lua {

template <typename T, typename B = tll::channel::Base<T>>
class LuaBase : public B
{
 protected:
	using Base = B;

	std::list<std::string> _preload;
	std::string _code;
	std::string _extra_path;
	static constexpr tll_channel_log_msg_format_t _dump_error = TLL_MESSAGE_LOG_FRAME;

	LuaRc _lua;

	tll::lua::Encoder _encoder;
	tll::lua::Settings _settings;
	enum class MessageMode { Auto, Reflection, Binary, Object } _message_mode = MessageMode::Auto;
 public:
	/// Close policy: perform cleanup in close or leave it to user
	enum class LuaClosePolicy { Cleanup, Skip };
	static constexpr auto lua_close_policy() { return LuaClosePolicy::Cleanup; }

	int _init(const tll::Channel::Url &url, tll::Channel *master)
	{
		auto reader = this->channel_props_reader(url);
		_code = reader.template getT<std::string>("code");
		_extra_path = reader.template getT<std::string>("path", "");
		auto scheme_control = reader.get("scheme-control");
		enum Preset { Filter, Convert, ConvertFast };
		auto preset = reader.getT("preset", Convert, {{"filter", Filter}, {"convert", Convert}, {"convert-fast", ConvertFast}});
		_settings.pmap_mode = reader.getT("pmap-mode", Settings::PMap::Enable);
		switch (preset) {
		case Filter:
			_settings.enum_mode = Settings::Enum::String;
			_settings.bits_mode = Settings::Bits::Object;
			_settings.fixed_mode = Settings::Fixed::Float;
			_settings.decimal128_mode = Settings::Decimal128::Float;
			_settings.time_mode = Settings::Time::Object;
			break;
		case Convert:
			_settings.enum_mode = Settings::Enum::String;
			_settings.bits_mode = Settings::Bits::Object;
			_settings.fixed_mode = Settings::Fixed::Object;
			_settings.decimal128_mode = Settings::Decimal128::Object;
			_settings.time_mode = Settings::Time::Object;
			break;
		case ConvertFast:
			_settings.enum_mode = Settings::Enum::Int;
			_settings.bits_mode = Settings::Bits::Int;
			_settings.fixed_mode = Settings::Fixed::Int;
			_settings.decimal128_mode = Settings::Decimal128::Object;
			_settings.time_mode = Settings::Time::Int;
			break;
		}
		_settings.child_mode = reader.getT("child-mode", _settings.child_mode);
		_settings.enum_mode = reader.getT("enum-mode", _settings.enum_mode);
		_settings.bits_mode = reader.getT("bits-mode", _settings.bits_mode);
		_settings.fixed_mode = reader.getT("fixed-mode", _settings.fixed_mode);
		_settings.decimal128_mode = reader.getT("decimal128-mode", _settings.decimal128_mode);
		_settings.time_mode = reader.getT("time-mode", _settings.time_mode);

		_encoder.fixed_mode = _settings.fixed_mode;
		_encoder.time_mode = _settings.time_mode;
		_encoder.overflow_mode = reader.getT("overflow-mode", Encoder::Overflow::Error);

		_message_mode = reader.getT("message-mode", MessageMode::Auto, {{"auto", MessageMode::Auto}, {"reflection", MessageMode::Reflection}, {"binary", MessageMode::Binary}, {"object", MessageMode::Object}});
		if (!reader)
			return this->_log.fail(EINVAL, "Invalid url: {}", reader.error());

		if (scheme_control) {
			this->_scheme_control.reset(this->context().scheme_load(*scheme_control));
			if (!this->_scheme_control)
				return this->_log.fail(EINVAL, "Failed to load control scheme");
		}

		if (_extra_path.size())
			_extra_path += ";";

		for (auto [k, c] : url.browse("lua.path.**")) {
			auto v = c.get();
			if (!v || v->empty()) continue;
			_extra_path += std::string(*v) + ";";
		}

		for (auto [k, c] : url.browse("lua.preload.**")) {
			auto v = c.get();
			if (!v || v->empty()) continue;
			_preload.push_back(std::string(*v));
		}

		return Base::_init(url, master);
	}

	int _lua_open()
	{
		LuaRc lua(luaL_newstate());
		if (!lua)
			return this->_log.fail(EINVAL, "Failed to create lua state");

		luaL_openlibs(lua);

		LuaT<reflection::Array>::init(lua);
		LuaT<reflection::Message>::init(lua);
		LuaT<reflection::Message::Iterator>::init(lua);
		LuaT<reflection::Union>::init(lua);
		LuaT<reflection::Bits>::init(lua);
		LuaT<reflection::Decimal128>::init(lua);
		LuaT<reflection::Fixed>::init(lua);
		LuaT<reflection::Enum>::init(lua);
		LuaT<tll::lua::TimePoint>::init(lua);

		LuaT<scheme::Scheme>::init(lua);
		LuaT<scheme::Message>::init(lua);
		LuaT<scheme::Field>::init(lua);
		LuaT<scheme::Enum>::init(lua);
		LuaT<scheme::Bits>::init(lua);
		LuaT<scheme::Options>::init(lua);

		LuaT<tll::lua::Context>::init(lua);
		LuaT<tll::lua::Channel>::init(lua);
		LuaT<tll::lua::Logger>::init(lua);
		LuaT<tll::lua::Message>::init(lua);

		LuaT<tll::lua::Config>::init(lua);

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

		for (auto & code : _preload) {
			if (_lua_load(lua, code))
				return this->_log.fail(EINVAL, "Failed to load extra code");
		}

		if (_lua_load(lua, _code))
			return this->_log.fail(EINVAL, "Failed to load main code");

		lua_pushcfunction(lua, MetaT<reflection::Message>::copy);
		lua_setglobal(lua, "tll_msg_copy");

		lua_pushcfunction(lua, MetaT<reflection::Message>::deepcopy);
		lua_setglobal(lua, "tll_msg_deepcopy");

		lua_pushcfunction(lua, MetaT<reflection::Message>::pmap_check);
		lua_setglobal(lua, "tll_msg_pmap_check");

		lua_pushcfunction(lua.get(), tll::lua::TimePoint::create);
		lua_setglobal(lua.get(), "tll_time_point");

		lua_pushlightuserdata(lua, this->channelT());
		lua_pushcclosure(lua, _lua_callback, 1);
		lua_setglobal(lua, "tll_callback");

		luaT_push<tll::lua::Logger>(lua, { tll_logger_copy(this->_log.ptr()) });
		lua_setglobal(lua, "tll_logger");

		_lua = std::move(lua);

		return 0;
	}

	int _lua_load(lua_State * lua, const std::string &code)
	{
		if (code.substr(0, 7) == "file://") {
			auto filename = code.substr(7);
			if (luaL_loadfile(lua, filename.c_str()))
				return this->_log.fail(EINVAL, "Failed to load file '{}': {}", filename, lua_tostring(lua, -1));
		} else {
			if (luaL_loadstring(lua, code.c_str()))
				return this->_log.fail(EINVAL, "Failed to load source code {}:\n{}", lua_tostring(lua, -1), code);
		}

		if (lua_pcall(lua, 0, LUA_MULTRET, 0))
			return this->_log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));
		return 0;
	}


	int _close(bool force = false)
	{
		if (this->channelT()->lua_close_policy() == LuaClosePolicy::Cleanup)
			_lua_close();
		return Base::_close(force);
	}

	void _lua_close()
	{
		if (_lua)
			_lua_on_close();

		_lua.reset();
	}

	int _lua_on_open(const tll::ConstConfig &props)
	{
		lua_getglobal(_lua, "tll_on_open");
		if (lua_isfunction(_lua, -1)) {
			auto ref = _lua.copy();
			auto cfg = props.sub("lua").value_or(tll::Config());
			luaT_push(ref, tll::lua::Config { tll_config_ref(cfg) });
			if (lua_pcall(ref, 1, 0, 0))
				return this->_log.fail(EINVAL, "Lua open (tll_on_open) failed: {}", lua_tostring(ref, -1));
		}
		return 0;
	}

	void _lua_on_close()
	{
		lua_getglobal(_lua, "tll_on_close");
		if (lua_isfunction(_lua, -1)) {
			auto ref = _lua.copy();
			if (lua_pcall(ref, 0, 0, 0))
				this->_log.warning("Lua close (tll_on_close) failed: {}", lua_tostring(ref, -1));
		}
	}

	int _lua_pushmsg(const tll_msg_t * msg, const tll::Scheme * scheme, const tll::Channel * channel, bool skip_type = false)
	{
		const auto skip_index = skip_type ? 0 : 1;
		auto guard = StackGuard(_lua);
		if (!skip_type)
			lua_pushinteger(_lua, msg->type);
		lua_pushinteger(_lua, msg->seq);

		if (msg->type != TLL_MESSAGE_DATA)
			scheme = channel->scheme(msg->type);
		auto message = scheme ? scheme->lookup(msg->msgid) : nullptr;

		auto mode = _message_mode;
		if (mode == MessageMode::Auto && !scheme)
			mode = MessageMode::Binary;

		switch (mode) {
		case MessageMode::Object:
			if (message)
				lua_pushstring(_lua, message->name);
			else
				lua_pushinteger(_lua, msg->msgid);
			luaT_push(_lua, tll::lua::Message { msg, message, _settings });
			break;
		case MessageMode::Reflection:
		case MessageMode::Auto:
			if (!message)
				return this->_log.fail(-1, "Message {} not found", msg->msgid);
			if (msg->size < message->size)
				return this->_log.fail(-1, "Message {} size too small: {} < minimum {}", message->name, msg->size, message->size);
			lua_pushstring(_lua, message->name);
			luaT_push(_lua, reflection::Message { message, tll::make_view(*msg), _settings });
			break;
		case MessageMode::Binary:
			if (message)
				lua_pushstring(_lua, message->name);
			else
				lua_pushnil(_lua);
			lua_pushlstring(_lua, (const char *) msg->data, msg->size);
			break;
		}

		lua_pushinteger(_lua, msg->msgid);
		lua_pushinteger(_lua, msg->addr.i64);
		lua_pushinteger(_lua, msg->time);
		guard.release();
		return 6 + skip_index;
	}

	static T * _lua_self(lua_State * lua, int index)
	{
		return (T *) lua_touserdata(lua, lua_upvalueindex(index));
	}

	static int _lua_callback(lua_State * lua)
	{
		if (auto self = _lua_self(lua, 1); self) {
			auto msg = self->_encoder.encode_stack(lua, self->_scheme.get(), self->self(), 0);
			if (!msg) {
				self->_log.error("Failed to convert message: {}", self->_encoder.error);
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
