/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_ENCODER_H
#define _TLL_LUA_ENCODER_H

#include "luat.h"
#include "reflection.h"

#include <tll/channel.h>
#include <tll/scheme.h>
#include <tll/scheme/error-stack.h>

namespace tll::lua {

struct Encoder : public tll::scheme::ErrorStack
{
	tll_msg_t msg;
	std::vector<char> buf;

	tll_msg_t * encode_data(lua_State * lua, tll_msg_t &msg, const tll::scheme::Message * message, int index)
	{
		if (lua_isstring(lua, index)) {
			auto data = luaT_tostringview(lua, index);
			msg.data = data.data();
			msg.size = data.size();
			return &msg;
		}

		if (auto data = luaT_testudata<reflection::Message>(lua, index); data) {
			if (!message || message == data->message) {
				msg.msgid = data->message->msgid;
				msg.data = data->data.data();
				msg.size = data->data.size();
				return &msg;
			}
		} else if (!lua_istable(lua, index)) {
			return fail(nullptr, "Invalid type of data: allowed string, table and Message");
		}

		if (!message)
			return fail(nullptr, "Table body without message scheme not supported");

		buf.resize(0);
		buf.resize(message->size);
		auto view = tll::make_view(buf);

		if (encode(message, view, lua, index))
			return fail(nullptr, "Failed to encode Lua message at {}: {}", format_stack(), error);

		msg.data = buf.data();
		msg.size = buf.size();

		return &msg;
	}


	tll_msg_t * encode_stack(lua_State * lua, const tll::Scheme * scheme, const tll::Channel * channel, int offset)
	{
		auto index = offset + 1;
		const auto args = lua_gettop(lua);

		msg = { TLL_MESSAGE_DATA };
		const tll::scheme::Message * message = nullptr;

		if (lua_istable(lua, index)) {
			if (args > index + 1)
				return fail(nullptr, "Extra arguments not supported when using table: {} extra args", args - index - 1);

			luaT_pushstringview(lua, "type");
			if (auto type = lua_gettable(lua, index); type == LUA_TSTRING) {
				auto s = luaT_tostringview(lua, -1);
				if (s == "Control")
					msg.type = TLL_MESSAGE_CONTROL;
				else if (s != "Data")
					return fail(nullptr, "Unknown message type: '{}', need one of Data or Control", s);
			} else if (type == LUA_TNUMBER)
				msg.type = lua_tointeger(lua, -1);
			lua_pop(lua, 1);
			if (msg.type != TLL_MESSAGE_DATA)
				scheme = channel->scheme(msg.type);

			luaT_pushstringview(lua, "seq");
			if (auto type = lua_gettable(lua, index); type == LUA_TNUMBER)
				msg.seq = lua_tointeger(lua, -1);
			lua_pop(lua, 1);

			auto with_name = false;
			luaT_pushstringview(lua, "name");
			if (auto type = lua_gettable(lua, index); type == LUA_TSTRING) {
				with_name = true;
				auto name = luaT_tostringview(lua, -1);
				if (scheme) {
					message = scheme->lookup(name);
					if (!message)
						return fail(nullptr, "Message '{}' not found", name);
					msg.msgid = message->msgid;
				} else
					return fail(nullptr, "Message name '{}' without scheme", name);
			} else if (type != LUA_TNIL)
				return fail(nullptr, "Invalid type of 'name' parameter: {}", type);
			lua_pop(lua, 1);

			luaT_pushstringview(lua, "msgid");
			if (auto type = lua_gettable(lua, index); type == LUA_TNUMBER) {
				if (with_name)
					return fail(nullptr, "Conflicting 'name' and 'msgid' parameters in table, need only one");
				auto msgid = lua_tointeger(lua, -1);
				if (scheme) {
					message = scheme->lookup(msgid);
					if (!message)
						return fail(nullptr, "Message '{}' not found", msgid);
				}
				msg.msgid = msgid;
			} else if (type != LUA_TNIL)
				return fail(nullptr, "Invalid type of 'msgid' parameter: {}", type);
			lua_pop(lua, 1);

			luaT_pushstringview(lua, "addr");
			if (auto type = lua_gettable(lua, index); type == LUA_TNUMBER)
				msg.addr.i64 = lua_tointeger(lua, -1);
			else if (type != LUA_TNIL)
				return fail(nullptr, "Invalid type of 'addr' parameter: {}", type);
			lua_pop(lua, 1);

			luaT_pushstringview(lua, "data");
			if (auto type = lua_gettable(lua, index); type != LUA_TNIL) {
				if (auto r = encode_data(lua, msg, message, lua_gettop(lua)); !r)
					return r;
			}
			lua_pop(lua, 1);
			return &msg;
		}

		if (args < index + 2)
			return fail(nullptr, "Too small number of arguments: {} < min {}", args, index + 2);

		if (lua_isinteger(lua, index))
			msg.seq = lua_tointeger(lua, index);
		index++;

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

		return encode_data(lua, msg, message, index);
	}

	template <typename Buf>
	int encode(const tll::scheme::Message * message, Buf view, lua_State * lua, int index)
	{
		auto pmap = message->pmap;
		auto pmap_view = pmap ? view.view(pmap->offset) : view;
		for (auto f = message->fields; f; f = f->next) {
			luaT_pushstringview(lua, f->name);
			if (lua_gettable(lua, index) == LUA_TNIL) {
				lua_pop(lua, 1);
				continue;
			}

			if (pmap)
				tll_scheme_pmap_set(pmap_view.data(), f->index);

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
		case Field::Decimal128: {
			if (!lua_isstring(lua, -1))
				return fail(EINVAL, "Non-string data for decimal128");
			auto data = luaT_tostringview(lua, -1);
			if (data.size() != field->size)
				return fail(ERANGE, "Decimal128 binary blob size mismatch: expected {}, got {}", field->size, data.size());
			memcpy(view.data(), data.data(), data.size());
		}
		case Field::Bytes: {
			if (!lua_isstring(lua, -1))
				return fail(EINVAL, "Non-string data for bytes field");
			auto data = luaT_tostringview(lua, -1);
			if (data.size() > field->size)
				return fail(ERANGE, "String too long: {} > max {}", data.size(), field->size);
			memcpy(view.data(), data.data(), data.size());
			return 0;
		}
		case Field::Array: {
			auto size = luaL_len(lua, -1);
			if (size < 0)
				return fail(ERANGE, "Negative array size: {}", size);
			if ((size_t) size > field->count)
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

			if (auto r = luaL_len(lua, -1); r < 0)
				return fail(ERANGE, "Negative array size: {}", r);
			else
				ptr.size = r;

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
	int encode_bits(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		T value = 0;
		auto type = lua_type(lua, -1);
		if (type == LUA_TNUMBER)
			return encode_numeric_raw<T>(field, view, lua);
		else if (type != LUA_TTABLE && type != LUA_TUSERDATA)
			return fail(EINVAL, "Only integer or table types supported for Bits, got {}", type);
		for (auto bit = field->type_bits->values; bit; bit = bit->next) {
			lua_pushstring(lua, bit->name);
			auto type = lua_gettable(lua, -2);
			T v = 0;
			switch (type) {
			case LUA_TNIL:
				break;
			case LUA_TNUMBER:
				v = lua_tointeger(lua, -1);
				break;
			case LUA_TBOOLEAN:
				v = lua_toboolean(lua, -1) ? 1 : 0;
				break;
			default:
				lua_pop(lua, 1);
				return fail(EINVAL, "Invalid type for bit member {}: {}", bit->name, type);
			}
			lua_pop(lua, 1);
			if (bit->size > 1)
				v &= (1 << bit->size) - 1;
			else
				v = v ? 1 : 0;
			value |= v << bit->offset;
		}
		*view.template dataT<T>() = value;
		return 0;
	}

	template <typename T, typename Buf>
	int encode_numeric(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		switch (field->sub_type) {
		case tll::scheme::Field::SubNone:
			break;
		case tll::scheme::Field::Bits:
			if constexpr (!std::is_same_v<double, T>)
				return encode_bits<T>(field, view, lua);
			break;
		default:
			break;
		}
		return encode_numeric_raw<T>(field, view, lua);
	}

	template <typename T, typename Buf>
	int encode_numeric_raw(const tll::scheme::Field * field, Buf view, lua_State * lua)
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
