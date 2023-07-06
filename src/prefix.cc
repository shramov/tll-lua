/*
 * Copyright (c) 2023 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "prefix.h"

int LuaPrefix::_open(const tll::ConstConfig &props)
{
	if (auto r = _lua_open(); r)
		return r;

	lua_pushlightuserdata(_lua, this);
	lua_setglobal(_lua, "luatll_self");

	lua_pushcfunction(_lua, _lua_post);
	lua_setglobal(_lua, "luatll_post");

	lua_pushcfunction(_lua, _lua_callback);
	lua_setglobal(_lua, "luatll_callback");

	lua_getglobal(_lua, "luatll_on_data");
	_with_on_data = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "luatll_on_post");
	_with_on_post = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "luatll_open");
	if (lua_isfunction(_lua, -1)) {
		if (lua_pcall(_lua, 0, 0, 0))
			return _log.fail(EINVAL, "Lua open (luatll_open) failed: {}", lua_tostring(_lua, -1));
	}

	return Base::_open(props);
}

int LuaPrefix::_close(bool force)
{
	if (_lua) {
		lua_getglobal(_lua, "luatll_close");
		if (lua_isfunction(_lua, -1)) {
			if (lua_pcall(_lua, 0, 0, 0))
				_log.warning("Lua close (luatll_close) failed: {}", lua_tostring(_lua, -1));
		}
	}

	return Base::_close(force);
}

int LuaPrefix::_on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, std::string_view func)
{
	std::string_view name;
	lua_getglobal(_lua, func.data());
	lua_pushinteger(_lua, msg->seq);
	if (scheme) {
		auto message = scheme->lookup(msg->msgid);
		if (!message) {
			lua_settop(_lua, 0);
			return _log.fail(ENOENT, "Message {} not found", msg->msgid);
		}
		name = message->name;
		lua_pushstring(_lua, message->name);
		luaT_push(_lua, reflection::Message { message, tll::make_view(*msg) });
	} else {
		lua_pushnil(_lua);
		lua_pushlstring(_lua, (const char *) msg->data, msg->size);
	}
	lua_pushinteger(_lua, msg->msgid);
	lua_pushinteger(_lua, msg->addr.i64);
	lua_pushinteger(_lua, msg->time);
	//luaT_push(_lua, msg);
	if (lua_pcall(_lua, 6, 1, 0)) {
		_log.warning("Lua function {} failed for {}:{}: {}", func, name, msg->seq, lua_tostring(_lua, -1));
		lua_settop(_lua, 0);
		return EINVAL;
	}
	lua_settop(_lua, 0);
	return 0;
}

tll_msg_t * LuaPrefix::_lua_msg(lua_State * lua, const tll::Scheme * scheme)
{
	auto args = lua_gettop(lua);
	if (args < 3)
		return _log.fail(nullptr, "Too small number of arguments: {}", args);
	_msg = { TLL_MESSAGE_DATA };

	if (lua_isinteger(lua, 1))
		_msg.seq = lua_tointeger(lua, 1);

	const tll::scheme::Message * message = nullptr;
	if (lua_isnil(lua, 2)) {
		// Pass
	} else if (lua_isinteger(lua, 2)) {
		_msg.msgid = lua_tointeger(lua, 2);
		if (scheme) {
			message = scheme->lookup(_msg.msgid);
			if (!message)
				return _log.fail(nullptr, "Message '{}' not found in scheme", name);
		}
	} else if (lua_isstring(lua, 1)) {
		if (!scheme)
			return _log.fail(nullptr, "Message name '{}' without scheme", name);
		auto name = luaT_tostringview(lua, 2);
		message = scheme->lookup(name);
		if (!message)
			return _log.fail(nullptr, "Message '{}' not found in scheme", name);
		_msg.msgid = message->msgid;
	} else
		return _log.fail(nullptr, "Invalid message name/id argument");

	if (args > 4 && lua_isinteger(lua, 4))
		_msg.addr.i64 = lua_tointeger(lua, 4);

	if (lua_isstring(lua, 3)) {
		auto data = luaT_tostringview(lua, 3);
		_msg.data = data.data();
		_msg.size = data.size();
		return &_msg;
	}

	if (auto data = luaT_testuserdata<reflection::Message>(lua, 3); data) {
		if (!message || message == data->message) {
			_msg.msgid = data->message->msgid;
			_msg.data = data->data.data();
			_msg.size = data->data.size();
			return &_msg;
		}
	} else if (!lua_istable(lua, 3)) {
		return _log.fail(nullptr, "Invalid type of data: allowed string, table and Message");
	}

	_buf.resize(0);
	_buf.resize(message->size);
	auto view = tll::make_view(_buf);
	if (_fill(message, view, lua, 3))
		return _log.fail(nullptr, "Failed to fill message {} from Lua", message->name);
	_msg.data = _buf.data();
	_msg.size = _buf.size();

	return &_msg;
}

template <typename Buf>
int LuaPrefix::_fill(const tll::scheme::Message * message, Buf view, lua_State * lua, int index)
{
	for (auto f = message->fields; f; f = f->next) {
		luaT_pushstringview(lua, f->name);
		if (lua_gettable(lua, index) == LUA_TNIL) {
			lua_pop(lua, 1);
			continue;
		}
		auto fview = view.view(f->offset);
		auto r = _fill(f, fview, lua, index);
		lua_pop(lua, 1);
		if (r)
			return _log.fail(EINVAL, "Failed to fill field {} from Lua", f->name);
	}
	return 0;
}

template <typename Buf>
int LuaPrefix::_fill(const tll::scheme::Field * field, Buf view, lua_State * lua, int index)
{
	using Field = tll::scheme::Field;
	switch (field->type) {
	case Field::Int8: return _fill_numeric<int8_t>(field, view, lua);
	case Field::Int16: return _fill_numeric<int16_t>(field, view, lua);
	case Field::Int32: return _fill_numeric<int32_t>(field, view, lua);
	case Field::Int64: return _fill_numeric<int64_t>(field, view, lua);
	case Field::UInt8: return _fill_numeric<uint8_t>(field, view, lua);
	case Field::UInt16: return _fill_numeric<uint16_t>(field, view, lua);
	case Field::UInt32: return _fill_numeric<uint32_t>(field, view, lua);
	case Field::UInt64: return _fill_numeric<uint64_t>(field, view, lua);
	case Field::Double: return _fill_numeric<double>(field, view, lua);
	case Field::Decimal128: return _log.fail(EINVAL, "Decimal128 not supported");
	case Field::Bytes: {
		if (!lua_isstring(lua, -1))
			return _log.fail(EINVAL, "Non-string data for field {}", field->name);
		auto data = luaT_tostringview(lua, -1);
		if (data.size() > field->size)
			return _log.fail(ERANGE, "String for field {} is too long: {} > max {}", field->name, data.size(), field->size);
		memcpy(view.data(), data.data(), data.size());
		return 0;
	}
	case Field::Array: {
		auto size = lua_rawlen(lua, -1);
		if (size > field->count)
			return _log.fail(ERANGE, "Array length for field {} is too long: {} > max {}", field->name, size, field->count);
		tll::scheme::write_size(field->count_ptr, view.view(field->count_ptr->offset), size);

		auto aview = view.view(field->type_array->offset);
		for (auto i = 0u; i < size; i++) {
			lua_pushinteger(lua, i + 1);
			if (lua_gettable(lua, -2) == LUA_TNIL) {
				lua_pop(lua, 1);
				continue;
			}
			auto fview = aview.view(field->type_array->size * i);
			auto r = _fill(field->type_array, fview, lua, lua_gettop(lua));
			lua_pop(lua, 1);
			if (r)
				return _log.fail(EINVAL, "Failed to fill field {}[{}] from Lua", field->name, i);
		}
		return 0;
	}
	case Field::Pointer: {
		tll::scheme::generic_offset_ptr_t ptr = {};
		ptr.offset = view.size();
		if (field->sub_type == Field::ByteString) {
			if (!lua_isstring(lua, -1))
				return _log.fail(EINVAL, "Non-string data for field {}", field->name);
			auto data = luaT_tostringview(lua, -1);
			ptr.size = data.size() + 1;
			ptr.entity = 1;
			tll::scheme::write_pointer(field, view, ptr);

			view = view.view(ptr.offset);
			view.resize(ptr.size);
			memcpy(view.data(), data.data(), data.size());
			*view.view(data.size()).template dataT<char>() = '\0';
			return 0;
		}

		if (!lua_istable(lua, -1))
			return _log.fail(EINVAL, "Non-array type for field {}", field->name);
		ptr.size = lua_rawlen(lua, -1);

		auto af = field->type_ptr;
		ptr.entity = af->size;

		tll::scheme::write_pointer(field, view, ptr);
		view = view.view(ptr.offset);
		view.resize(ptr.size * ptr.entity);

		for (auto i = 0u; i < ptr.size; i++) {
			lua_pushinteger(lua, i + 1);
			if (lua_gettable(lua, -2) == LUA_TNIL) {
				lua_pop(lua, 1);
				continue;
			}
			auto fview = view.view(ptr.entity * i);
			auto r = _fill(field->type_ptr, fview, lua, lua_gettop(lua));
			lua_pop(lua, 1);
			if (r)
				return _log.fail(EINVAL, "Failed to fill field {}[{}] from Lua", field->name, i);
		}
		return 0;
	}
	case Field::Message:
		return _fill(field->type_msg, view, lua, lua_gettop(lua));
	case Field::Union: return _log.fail(EINVAL, "Union not supported");
	}

	return 0;
}

template <typename T, typename Buf>
int LuaPrefix::_fill_numeric(const tll::scheme::Field * field, Buf view, lua_State * lua)
{
	T value;
	if constexpr (std::is_same_v<T, double>) {
		if (!lua_isnumber(lua, -1))
			return _log.fail(EINVAL, "Non-number type for field {}", field->name);
		value = lua_tonumber(lua, -1);
	} else {
		if (!lua_isinteger(lua, -1))
			return _log.fail(EINVAL, "Non-number type for field {}", field->name);
		auto v = lua_tointeger(lua, -1);
		if constexpr (std::is_unsigned_v<T>) {
			if (v < 0)
				return _log.fail(EINVAL, "Negative value {} for field {}", v, field->name);
			if (std::numeric_limits<T>::max() < (unsigned long long) v)
				return _log.fail(EINVAL, "Value for {} too large: {} > max {}", field->name, v, std::numeric_limits<T>::max());
		} else {
			if (std::numeric_limits<T>::min() > v)
				return _log.fail(EINVAL, "Value for {} too small: {} < min {}", field->name, v, std::numeric_limits<T>::min());
			if (std::numeric_limits<T>::max() < v)
				return _log.fail(EINVAL, "Value for {} too large: {} > max {}", field->name, v, std::numeric_limits<T>::max());
		}
		value = (T) v;
	}

	*view.template dataT<T>() = value;
	return 0;
}
