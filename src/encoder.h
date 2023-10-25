/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_ENCODER_H
#define _TLL_LUA_ENCODER_H

#include "luat.h"

#include <tll/scheme/error-stack.h>

namespace tll::lua {

struct Encoder : public tll::scheme::ErrorStack
{
	tll_msg_t msg;
	std::vector<char> buf;

	tll_msg_t * encode_stack(lua_State * lua, const tll::Scheme * scheme, int offset)
	{
		auto index = offset + 1;

		auto args = lua_gettop(lua);
		if (args < index + 2)
			return fail(nullptr, "Too small number of arguments: {} < min {}", args, index + 2);
		msg = { TLL_MESSAGE_DATA };

		if (lua_isinteger(lua, index))
			msg.seq = lua_tointeger(lua, index);
		index++;

		const tll::scheme::Message * message = nullptr;
		if (lua_isnil(lua, index)) {
			// Pass
		} else if (lua_isinteger(lua, index)) {
			msg.msgid = lua_tointeger(lua, index);
			if (scheme) {
				message = scheme->lookup(msg.msgid);
				if (!message)
					return fail(nullptr, "Message '{}' not found in scheme", msg.msgid);
			}
		} else if (lua_isstring(lua, index)) {
			auto name = luaT_tostringview(lua, index);
			if (!scheme)
				return fail(nullptr, "Message name '{}' without scheme", name);
			message = scheme->lookup(name);
			if (!message)
				return fail(nullptr, "Message '{}' not found in scheme", name);
			msg.msgid = message->msgid;
		} else
			return fail(nullptr, "Invalid message name/id argument");
		index++;

		if (args > index + 1 && lua_isinteger(lua, index + 1))
			msg.addr.i64 = lua_tointeger(lua, index + 1);

		if (lua_isstring(lua, index)) {
			auto data = luaT_tostringview(lua, index);
			msg.data = data.data();
			msg.size = data.size();
			return &msg;
		}

		if (auto data = luaT_testuserdata<reflection::Message>(lua, index); data) {
			if (!message || message == data->message) {
				msg.msgid = data->message->msgid;
				msg.data = data->data.data();
				msg.size = data->data.size();
				return &msg;
			}
		} else if (!lua_istable(lua, index)) {
			return fail(nullptr, "Invalid type of data: allowed string, table and Message");
		}

		buf.resize(0);
		buf.resize(message->size);
		auto view = tll::make_view(buf);

		if (encode(message, view, lua, index))
			return fail(nullptr, "Failed to encode Lua message at {}: {}", format_stack(), error);

		msg.data = buf.data();
		msg.size = buf.size();

		return &msg;
	}

	template <typename Buf>
	int encode(const tll::scheme::Message * message, Buf view, lua_State * lua, int index)
	{
		for (auto f = message->fields; f; f = f->next) {
			luaT_pushstringview(lua, f->name);
			if (lua_gettable(lua, index) == LUA_TNIL) {
				lua_pop(lua, 1);
				continue;
			}
			auto fview = view.view(f->offset);
			auto r = encode(f, fview, lua, index);
			lua_pop(lua, 1);
			if (r)
				return fail_field(EINVAL, f);
		}
		return 0;
	}

	template <typename Buf>
	int encode(const tll::scheme::Field * field, Buf view, lua_State * lua, int index)
	{
		using Field = tll::scheme::Field;
		switch (field->type) {
		case Field::Int8: return encode_numeric<int8_t>(field, view, lua);
		case Field::Int16: return encode_numeric<int16_t>(field, view, lua);
		case Field::Int32: return encode_numeric<int32_t>(field, view, lua);
		case Field::Int64: return encode_numeric<int64_t>(field, view, lua);
		case Field::UInt8: return encode_numeric<uint8_t>(field, view, lua);
		case Field::UInt16: return encode_numeric<uint16_t>(field, view, lua);
		case Field::UInt32: return encode_numeric<uint32_t>(field, view, lua);
		case Field::UInt64: return encode_numeric<uint64_t>(field, view, lua);
		case Field::Double: return encode_numeric<double>(field, view, lua);
		case Field::Decimal128: return fail(EINVAL, "Decimal128 not supported");
		case Field::Bytes: {
			if (!lua_isstring(lua, -1))
				return fail(EINVAL, "Non-string data for string field");
			auto data = luaT_tostringview(lua, -1);
			if (data.size() > field->size)
				return fail(ERANGE, "String too long: {} > max {}", data.size(), field->size);
			memcpy(view.data(), data.data(), data.size());
			return 0;
		}
		case Field::Array: {
			auto size = lua_rawlen(lua, -1);
			if (size > field->count)
				return fail(ERANGE, "Array too long: {} > max {}", size, field->count);
			tll::scheme::write_size(field->count_ptr, view.view(field->count_ptr->offset), size);

			auto aview = view.view(field->type_array->offset);
			for (auto i = 0u; i < size; i++) {
				lua_pushinteger(lua, i + 1);
				if (lua_gettable(lua, -2) == LUA_TNIL) {
					lua_pop(lua, 1);
					continue;
				}
				auto fview = aview.view(field->type_array->size * i);
				auto r = encode(field->type_array, fview, lua, lua_gettop(lua));
				lua_pop(lua, 1);
				if (r)
					return fail_index(EINVAL, i);
			}
			return 0;
		}
		case Field::Pointer: {
			tll::scheme::generic_offset_ptr_t ptr = {};
			ptr.offset = view.size();
			if (field->sub_type == Field::ByteString) {
				if (!lua_isstring(lua, -1))
					return fail(EINVAL, "Non-string data");
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
				return fail(EINVAL, "Non-array type");
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
				auto r = encode(field->type_ptr, fview, lua, lua_gettop(lua));
				lua_pop(lua, 1);
				if (r)
					return fail_index(EINVAL, i);
			}
			return 0;
		}
		case Field::Message:
			return encode(field->type_msg, view, lua, lua_gettop(lua));
		case Field::Union: return fail(EINVAL, "Union not supported");
		}

		return 0;
	}

	template <typename T, typename Buf>
	int encode_numeric(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		T value;
		if constexpr (std::is_same_v<T, double>) {
			if (!lua_isnumber(lua, -1))
				return fail(EINVAL, "Non-number type");
			value = lua_tonumber(lua, -1);
		} else {
			if (!lua_isinteger(lua, -1))
				return fail(EINVAL, "Non-integer type");
			auto v = lua_tointeger(lua, -1);
			if constexpr (std::is_unsigned_v<T>) {
				if (v < 0)
					return fail(EINVAL, "Negative value {}", v);
				if (std::numeric_limits<T>::max() < (unsigned long long) v)
					return fail(EINVAL, "Value too large: {} > max {}", v, std::numeric_limits<T>::max());
			} else {
				if (std::numeric_limits<T>::min() > v)
					return fail(EINVAL, "Value too small: {} < min {}", v, std::numeric_limits<T>::min());
				if (std::numeric_limits<T>::max() < v)
					return fail(EINVAL, "Value too large: {} > max {}", v, std::numeric_limits<T>::max());
			}
			value = (T) v;
		}

		*view.template dataT<T>() = value;
		return 0;
	}
};

} // namespace tll::lua

#endif//_TLL_LUA_ENCODER_H
