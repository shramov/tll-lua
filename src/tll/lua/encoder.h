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
#include <tll/conv/numeric.h>
#include <tll/scheme.h>
#include <tll/scheme/error-stack.h>
#include <tll/util/time.h>

#include <cmath>
#include <fmt/format.h>

namespace tll::lua {

struct Encoder : public tll::scheme::ErrorStack
{
	tll_msg_t msg;
	std::vector<char> buf;
	Settings::Fixed fixed_mode = Settings::Fixed::Int;
	Settings::Time time_mode = Settings::Time::Object;
	enum class Overflow { Error, Trim } overflow_mode = Overflow::Error;

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

		if (auto * ptr = luaT_testudata<tll::lua::Message>(lua, index); ptr) {
			msg = *ptr->ptr;
			return &msg;
		}

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

			luaT_pushstringview(lua, "time");
			if (auto type = lua_gettable(lua, index); type == LUA_TNUMBER)
				msg.time = lua_tointeger(lua, -1);
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
			auto ptr = view.template dataT<tll::util::Decimal128>();
			switch (auto type = lua_type(lua, -1); type) {
			case LUA_TUSERDATA:
				if (auto r = luaT_touserdata<reflection::Decimal128>(lua, -1)) {
					*ptr = r->data;
				} else
					return fail(EINVAL, "Non-decimal128 userdata");
				break;
			case LUA_TNUMBER: {
				auto f = lua_tonumber(lua, -1);
				auto i = lua_tointeger(lua, -1);
				if (f == i) {
					tll::util::Decimal128::Unpacked uf = {};
					uf.sign = (i < 0);
					uf.mantissa.lo = uf.sign ? -i : i;
					ptr->pack(uf);
					break;
				} else if (auto r = double2d128(ptr, f); r)
					return fail(EINVAL, "Invalid double value {}: {}", f, strerror(r));
				break;
			}
			case LUA_TSTRING: {
				auto s = luaT_tostringview(lua, -1);
				if (auto r = tll::conv::to_any<tll::util::Decimal128>(s); r) {
					*ptr = *r;
				} else
					return fail(EINVAL, "Invalid decimal128 string '{}': {}", s, r.error());
				break;
			}
			default:
				return fail(EINVAL, "Invalid type for decimal128, need string, number of decimal128, got {}", type);
			}
			return 0;
		}
		case Field::Bytes: {
			if (!lua_isstring(lua, -1))
				return fail(EINVAL, "Non-string data for bytes field");
			auto data = luaT_tostringview(lua, -1);
			if (data.size() > field->size) {
				if (overflow_mode == Overflow::Error)
					return fail(ERANGE, "String too long: {} > max {}", data.size(), field->size);
				data = data.substr(0, field->size);
			}
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
			if (field->sub_type == Field::ByteString) {
				if (!lua_isstring(lua, -1))
					return fail(EINVAL, "Non-string data");
				auto data = luaT_tostringview(lua, -1);
				ptr.size = data.size() + 1;
				ptr.entity = 1;
				if (tll::scheme::alloc_pointer(field, view, ptr))
					return fail(EINVAL, "Failed to allocate pointer");

				view = view.view(ptr.offset);
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

			if (tll::scheme::alloc_pointer(field, view, ptr))
				return fail(EINVAL, "Failed to allocate pointer");
			view = view.view(ptr.offset);

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
	int encode_enum(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		auto type = lua_type(lua, -1);
		auto ptr = view.template dataT<T>();
		if (type == LUA_TNUMBER)
			return encode_numeric_raw<T>(field, view, lua);
		else if (type == LUA_TUSERDATA) {
			if (auto r = luaT_touserdata<reflection::Enum>(lua, -1)) {
				*ptr = r->value;
				return 0;
			}
			return fail(EINVAL, "Non-Enum userdata");
		} else if (type == LUA_TSTRING) {
			auto str = luaT_tostringview(lua, -1);
			if (auto v = tll::scheme::lookup_name(field->type_enum->values, str); v) {
				*ptr = v->value;
				return 0;
			}
			return fail(EINVAL, "Unknown value for enum {}: '{}'", field->type_enum->name, str);
		} else
			return fail(EINVAL, "Only integer, string or userdata types supported for Enum, got {}", type);
	}

	template <typename T, typename Buf>
	int encode_numeric(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		switch (field->sub_type) {
		case tll::scheme::Field::SubNone:
			break;
		case tll::scheme::Field::Enum:
			if constexpr (!std::is_same_v<double, T>)
				return encode_enum<T>(field, view, lua);
			break;
		case tll::scheme::Field::Bits:
			if constexpr (!std::is_same_v<double, T>)
				return encode_bits<T>(field, view, lua);
			break;
		case tll::scheme::Field::Fixed:
			if constexpr (!std::is_same_v<double, T>)
				return encode_fixed<T>(field, view, lua);
			break;
		case tll::scheme::Field::TimePoint:
			return encode_time_point<T>(field, view, lua);
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
			int result = 0;
			auto v = lua_tointegerx(lua, -1, &result);
			if (!result)
				return fail(EINVAL, "Failed to convert value '{}' to integer", luaT_tostringview(lua, -1));
			if constexpr (std::is_unsigned_v<T>) {
				if (v < 0) {
					if (overflow_mode == Overflow::Error)
						return fail(EINVAL, "Negative value {}", v);
					v = 0;
				}
				if (std::numeric_limits<T>::max() < (unsigned long long) v) {
					if (overflow_mode == Overflow::Error)
						return fail(EINVAL, "Value too large: {} > max {}", v, std::numeric_limits<T>::max());
					v = std::numeric_limits<T>::max();
				}
			} else {
				if (std::numeric_limits<T>::min() > v) {
					if (overflow_mode == Overflow::Error)
						return fail(EINVAL, "Value too small: {} < min {}", v, std::numeric_limits<T>::min());
					v = std::numeric_limits<T>::min();
				}
				if (std::numeric_limits<T>::max() < v) {
					if (overflow_mode == Overflow::Error)
						return fail(EINVAL, "Value too large: {} > max {}", v, std::numeric_limits<T>::max());
					v = std::numeric_limits<T>::max();
				}
			}
			value = (T) v;
		}

		*view.template dataT<T>() = value;
		return 0;
	}

	template <typename T, typename Buf>
	int encode_fixed(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		auto type = lua_type(lua, -1);
		if (type == LUA_TNUMBER) {
			if (fixed_mode == Settings::Fixed::Int) {
				int result = 0;
				auto v = lua_tointegerx(lua, -1, &result);
				if (!result)
					return fail(EINVAL, "Failed to convert value '{}' to integer", luaT_tostringview(lua, -1));
				*view.template dataT<T>() = v;
			} else if (fixed_mode == Settings::Fixed::Float || fixed_mode == Settings::Fixed::Object) {
				auto v = lua_tonumber(lua, -1);
				*view.template dataT<T>() = v * intpow(10, field->fixed_precision);
			}
		} else if (type == LUA_TSTRING) {
			auto s = luaT_tostringview(lua, -1);
			auto u = tll::conv::to_any<tll::conv::unpacked_float<T>>(s);
			if (!u)
				return fail(EINVAL, "Failed to parse numeric string '{}': {}", s, u.error());
			if (u->sign)
				u->mantissa = -u->mantissa;
			u->exponent += field->fixed_precision;
			if (u->exponent >= 0)
				u->mantissa *= intpow(10, u->exponent);
			else
				u->mantissa /= intpow(10, u->exponent);
			*view.template dataT<T>() = u->mantissa;
		} else if (type == LUA_TUSERDATA) {
			using tll::scheme::Field;
			auto obj = luaT_touserdata<reflection::Fixed>(lua, -1);
			if (!obj)
				return fail(EINVAL, "Non-Fixed userdata");
			unsigned long long mul = 1;
			unsigned long long div = 1;
			if (int dprec = field->fixed_precision - obj->field->fixed_precision; dprec > 0)
				mul = intpow(10, dprec);
			else
				div = intpow(10, -dprec);
			switch (obj->field->type) {
			case Field::Int8:  *view.template dataT<T>() = *obj->data.template dataT<int8_t>() * mul / div; break;
			case Field::Int16: *view.template dataT<T>() = *obj->data.template dataT<int16_t>() * mul / div; break;
			case Field::Int32: *view.template dataT<T>() = *obj->data.template dataT<int32_t>() * mul / div; break;
			case Field::Int64: *view.template dataT<T>() = *obj->data.template dataT<int64_t>() * mul / div; break;
			case Field::UInt8: *view.template dataT<T>() = *obj->data.template dataT<uint8_t>() * mul / div; break;
			case Field::UInt16: *view.template dataT<T>() = *obj->data.template dataT<uint16_t>() * mul / div; break;
			case Field::UInt32: *view.template dataT<T>() = *obj->data.template dataT<uint32_t>() * mul / div; break;
			case Field::UInt64: *view.template dataT<T>() = *obj->data.template dataT<uint64_t>() * mul / div; break;
			default:
				return fail(EINVAL, "Invalid type for Fixed field: {}", obj->field->type);
			}
		} else
			return fail(EINVAL, "Invalid type for fixed number: {}", type);
		return 0;
	}

	template <typename T, typename Buf>
	int encode_time_point(const tll::scheme::Field * field, Buf view, lua_State * lua)
	{
		switch (field->time_resolution) {
		case TLL_SCHEME_TIME_NS: return encode_time_point_raw<T, std::nano>(field, view.template dataT<T>(), lua);
		case TLL_SCHEME_TIME_US: return encode_time_point_raw<T, std::micro>(field, view.template dataT<T>(), lua);
		case TLL_SCHEME_TIME_MS: return encode_time_point_raw<T, std::milli>(field, view.template dataT<T>(), lua);
		case TLL_SCHEME_TIME_SECOND: return encode_time_point_raw<T, std::ratio<1, 1>>(field, view.template dataT<T>(), lua);
		case TLL_SCHEME_TIME_MINUTE: return encode_time_point_raw<T, std::ratio<60, 1>>(field, view.template dataT<T>(), lua);
		case TLL_SCHEME_TIME_HOUR: return encode_time_point_raw<T, std::ratio<3600, 1>>(field, view.template dataT<T>(), lua);
		case TLL_SCHEME_TIME_DAY: return encode_time_point_raw<T, std::ratio<86400, 1>>(field, view.template dataT<T>(), lua);
		}
		return fail(EINVAL, "Unsupported time resolution: {}", field->time_resolution);
	}

	template <typename T, typename Res>
	int encode_time_point_raw(const tll::scheme::Field * field, T * value, lua_State * lua)
	{
		auto type = lua_type(lua, -1);
		if (type == LUA_TNUMBER) {
			if (time_mode == Settings::Time::Int) {
				if constexpr (std::is_floating_point_v<T>) {
					*value = lua_tonumber(lua, -1);
				} else {
					int result = 0;
					auto v = lua_tointegerx(lua, -1, &result);
					if (!result)
						return fail(EINVAL, "Failed to convert value '{}' to integer", luaT_tostringview(lua, -1));
					*value = v;
				}
			} else {
				auto v = lua_tonumber(lua, -1);
				*value = v * Res::den / Res::num;
			}
		} else if (type == LUA_TSTRING) {
			auto s = luaT_tostringview(lua, -1);
			auto r = tll::conv::to_any<std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>>(s);
			if (!r)
				return fail(EINVAL, "Invalid datetime string '{}': {}", s, r.error());
			*value = std::chrono::duration_cast<std::chrono::duration<T, Res>>(r->time_since_epoch()).count();
		} else if (type == LUA_TUSERDATA) {
			auto obj = luaT_touserdata<tll::lua::TimePoint>(lua, -1);
			if (!obj)
				return fail(EINVAL, "Non-TimePoint userdata");
			if (obj->resolution == field->time_resolution) {
				switch (obj->type) {
				case tll::lua::TimePoint::Signed: *value = obj->vsigned; break;
				case tll::lua::TimePoint::Unsigned: *value = obj->vunsigned; break;
				case tll::lua::TimePoint::Double: *value = obj->vdouble; break;
				}
				return 0;
			}
			auto [mul, div] = obj->ratio();
			mul *= Res::den;
			div *= Res::num;
			if (mul >= div) {
				mul /= div;
				div = 1;
			} else {
				div /= mul;
				mul = 1;
			}
			if constexpr (std::is_floating_point_v<T>) {
				*value = obj->fvalue() * mul / div;
			} else {
				switch (obj->type) {
				case tll::lua::TimePoint::Signed: *value = obj->vsigned * mul / div; break;
				case tll::lua::TimePoint::Unsigned: *value = obj->vunsigned * mul / div; break;
				case tll::lua::TimePoint::Double: *value = obj->vdouble * mul / div; break;
				}
			}
		} else
			return fail(EINVAL, "Invalid type for fixed number: {}", type);
		return 0;
	}

	int double2d128(tll::util::Decimal128 * ptr, double from)
	{
		tll::util::Decimal128::Unpacked uf = {};
		switch (std::fpclassify(from)) {
		case FP_NAN:
			uf.exponent = uf.exp_nan;
			break;
		case FP_INFINITE:
			uf.sign = from < 0;
			uf.exponent = uf.exp_inf;
			break;
		case FP_ZERO:
			break;
		case FP_SUBNORMAL:
		case FP_NORMAL: {
#if FMT_VERSION < 70000
			auto s = fmt::format("{}", from);
			auto u = tll::conv::to_any<tll::conv::unpacked_float<uint64_t>>(s);
			if (!u)
				return EINVAL;
			return ptr->pack(u->sign, u->mantissa, u->exponent);
#else
			auto dec = fmt::detail::dragonbox::to_decimal(from);
			uf.sign = dec.significand < 0;
			uf.mantissa.lo = uf.sign ? -dec.significand : dec.significand;
			uf.exponent = dec.exponent;
#endif
			break;
		}
		default:
			return EINVAL;
		}
		return ptr->pack(uf);
	}
};

} // namespace tll::lua

template <>
struct tll::conv::parse<tll::lua::Encoder::Overflow>
{
	using Overflow = tll::lua::Encoder::Overflow;
        static result_t<Overflow> to_any(std::string_view s)
        {
                return tll::conv::select(s, std::map<std::string_view, Overflow> {
			{"error", Overflow::Error},
			{"trim", Overflow::Trim},
		});
        }
};

#endif//_TLL_LUA_ENCODER_H
