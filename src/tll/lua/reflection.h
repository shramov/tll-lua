/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_REFLECTION_H
#define _TLL_LUA_REFLECTION_H

#include "luat.h"

#include <tll/channel.h>
#include <tll/conv/decimal128.h>
#include <tll/scheme.h>
#include <tll/scheme/types.h>
#include <tll/scheme/util.h>
#include <tll/util/memoryview.h>

#include <limits>

namespace tll::lua {

struct Settings
{
	enum class Enum { Int, String, Object } enum_mode = Enum::Int;
	enum class Bits { Int, Object } bits_mode = Bits::Object;
	enum class Fixed { Int, Float, Object } fixed_mode = Fixed::Float;
};

namespace reflection {

struct Message
{
	struct Iterator
	{
		const Message * message = nullptr;
		const tll::scheme::Field * field = nullptr;
	};

	const tll::scheme::Message * message = nullptr;
	tll::memoryview<const tll_msg_t> data;
	const Settings & settings;

	const tll::scheme::Field * lookup(std::string_view name) const
	{
		for (auto f = message->fields; f; f = f->next)
			if (f->name == name)
				return f;
		return nullptr;
	}
};

struct Union
{
	const tll::scheme::Union * desc = nullptr;
	tll::memoryview<const tll_msg_t> data;
	const Settings & settings;
};

struct Array
{
	const tll::scheme::Field * field = nullptr;
	tll::memoryview<const tll_msg_t> data;
	const Settings & settings;

	int size(lua_State *lua) const
	{
		if (field->type == tll::scheme::Field::Array) {
			auto size = tll::scheme::read_size(field->count_ptr, data.view(field->count_ptr->offset));
			if (size < 0)
				return luaL_error(lua, "Array %s has invalid size: %d", field->name, size);
			return size;
		} else {
			auto ptr = tll::scheme::read_pointer(field, data);
			if (!ptr)
				return luaL_error(lua, "Unknown offset ptr version for %s: %d", field->name, field->offset_ptr_version);
			return ptr->size;
		}
	}

	int push(lua_State* lua, int key);
};

struct Bits
{
	const tll::scheme::Field * field = nullptr;
	tll::memoryview<const tll_msg_t> data;

	const tll_scheme_bit_field_t * lookup(std::string_view name) const
	{
		for (auto f = field->bitfields; f; f = f->next)
			if (f->name == name)
				return f;
		return nullptr;
	}
};

struct Decimal128
{
	tll::util::Decimal128 data;
};

struct Fixed
{
	const tll::scheme::Field * field = nullptr;
	tll::memoryview<const tll_msg_t> data;
};

struct Enum
{
	const tll::scheme::Enum * desc = nullptr;
	long long value;

	static const tll_scheme_enum_value_t * lookup(const tll::scheme::Enum * desc, long long value)
	{
		for (auto v = desc->values; v; v = v->next)
			if (v->value == value)
				return v;
		return nullptr;
	}

	const tll_scheme_enum_value_t * lookup(long long value) { return lookup(desc, value); }
};
} // namespace reflection

namespace {
inline constexpr unsigned long long intpow(unsigned base, unsigned pow)
{
	unsigned long long r = 1;
	while (pow-- > 0)
		r *= base;
	return r;
}

template <typename View, typename T>
int pushnumber(lua_State * lua, const tll::scheme::Field * field, View data, T v, const Settings & settings)
{
	if (field->sub_type == field->Bits) {
		switch (settings.bits_mode) {
		case Settings::Bits::Int:
			lua_pushinteger(lua, v);
			break;
		case Settings::Bits::Object:
			luaT_push<reflection::Bits>(lua, { field, data });
			break;
		}
	} else if (field->sub_type == field->Enum) {
		switch (settings.enum_mode) {
		case Settings::Enum::Int:
			lua_pushinteger(lua, v);
			break;
		case Settings::Enum::String:
			if (auto e = reflection::Enum::lookup(field->type_enum, v); e)
				lua_pushstring(lua, e->name);
			else
				lua_pushinteger(lua, v);
			break;
		case Settings::Enum::Object:
			luaT_push<reflection::Enum>(lua, { field->type_enum, (long long) v });
			break;
		}
	} else if (field->sub_type == field->Fixed) {
		switch (settings.fixed_mode) {
		case Settings::Fixed::Int:
			lua_pushinteger(lua, v);
			break;
		case Settings::Fixed::Float:
			lua_pushnumber(lua, ((double) v) / intpow(10, field->fixed_precision));
			break;
		case Settings::Fixed::Object:
			luaT_push<reflection::Fixed>(lua, { field, data });
			break;
		}
	} else
		lua_pushinteger(lua, v);
	return 1;
}

template <typename View>
int pushdouble(lua_State * lua, const tll::scheme::Field * field, View data, double v)
{
	lua_pushnumber(lua, v);
	return 1;
}

template <typename View>
int pushfield(lua_State * lua, const tll::scheme::Field * field, View data, const Settings & settings)
{
	using tll::scheme::Field;
	switch (field->type) {
	case Field::Int8:  return pushnumber(lua, field, data, *data.template dataT<int8_t>(), settings);
	case Field::Int16: return pushnumber(lua, field, data, *data.template dataT<int16_t>(), settings);
	case Field::Int32: return pushnumber(lua, field, data, *data.template dataT<int32_t>(), settings);
	case Field::Int64: return pushnumber(lua, field, data, *data.template dataT<int64_t>(), settings);
	case Field::UInt8:  return pushnumber(lua, field, data, *data.template dataT<uint8_t>(), settings);
	case Field::UInt16: return pushnumber(lua, field, data, *data.template dataT<uint16_t>(), settings);
	case Field::UInt32: return pushnumber(lua, field, data, *data.template dataT<uint32_t>(), settings);
	case Field::UInt64: return pushnumber(lua, field, data, *data.template dataT<uint64_t>(), settings);
	case Field::Double: return pushdouble(lua, field, data, *data.template dataT<double>());
	case Field::Decimal128:
		luaT_push<reflection::Decimal128>(lua, { *data.template dataT<tll::util::Decimal128>() });
		break;
	case Field::Bytes: {
		auto ptr = data.template dataT<const char>();
		if (field->sub_type == Field::ByteString)
			lua_pushlstring(lua, ptr, strnlen(ptr, field->size));
		else
			lua_pushlstring(lua, ptr, field->size);
		break;
	}
	case Field::Array:
		luaT_push<reflection::Array>(lua, { field, data, settings });
		break;
	case Field::Pointer:
		if (field->sub_type == Field::ByteString) {
			auto ptr = tll::scheme::read_pointer(field, data);
			if (!ptr)
				return luaL_error(lua, "Unknown offset ptr version for %s: %d", field->name, field->offset_ptr_version);
			lua_pushlstring(lua, data.view(ptr->offset).template dataT<const char>(), ptr->size ? ptr->size - 1 : 0);
		} else
			luaT_push<reflection::Array>(lua, { field, data, settings });
		break;
	case Field::Message:
		luaT_push<reflection::Message>(lua, { field->type_msg, data, settings });
		break;
	case Field::Union:
		luaT_push<reflection::Union>(lua, { field->type_union, data, settings });
		break;
	}
	return 1;
}
} // namespace reflection

template <>
struct MetaT<reflection::Message> : public MetaBase
{
	static constexpr std::string_view name = "reflection_message";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Message>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (r.data.size() < r.message->size)
			return luaL_error(lua, "Message '%s' size %d > data size %d", r.message->name, r.message->size, r.data.size());
		auto field = r.lookup(key);
		if (field == nullptr)
			return luaL_error(lua, "Message '%s' has no field '%s'", r.message->name, key.data());

		if (r.message->pmap) {
			auto pmap = r.data.view(r.message->pmap->offset);
			if (!tll::scheme::pmap_get(pmap.data(), field->index)) {
				lua_pushnil(lua);
				return 1;
			}
		}
		return pushfield(lua, field, r.data.view(field->offset), r.settings);
	}

	static int pairs(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Message>(lua, 1);
		lua_pushcfunction(lua, next);
		luaT_push(lua, reflection::Message::Iterator { &r, r.message->fields });
		lua_pushnil(lua);
		return 3;
	}

	static int next(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Message::Iterator>(lua, 1);
		if (!r.field)
			return 0;
		lua_pushstring(lua, r.field->name);
		pushfield(lua, r.field, r.message->data.view(r.field->offset), r.message->settings);
		r.field = r.field->next;
		return 2;
	}

	static int copy(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Message>(lua, 1);
		lua_newtable(lua);
		for (auto f = r.message->fields; f; f = f->next) {
			luaT_pushstringview(lua, f->name);
			pushfield(lua, f, r.data.view(f->offset), r.settings);
			lua_settable(lua, -3);
		}
		return 1;
	}
};

template <>
struct MetaT<reflection::Message::Iterator> : public MetaBase
{
	static constexpr std::string_view name = "reflection_message_iterator";
};

template <>
struct MetaT<reflection::Union> : public MetaBase
{
	static constexpr std::string_view name = "reflection_union";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Union>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		//if (r.data.size() < r.message->size)
		//	return luaL_error(lua, "Union '%s' size %d > data size %d", r.desc->name, r.desc->size, r.data.size());
		auto type = tll::scheme::read_size(r.desc->type_ptr, r.data.view(r.desc->type_ptr->offset));
		if (type < 0)
			return luaL_error(lua, "Union '%s' has invalid type field", r.desc->name);

		if ((size_t) type > r.desc->fields_size)
			return luaL_error(lua, "Union '%s' type %d is out of range %d", r.desc->name, type, r.desc->fields_size);
		auto field = r.desc->fields + type;

		if (key == "_tll_type")
			luaT_pushstringview(lua, field->name);
		else if (key == field->name)
			pushfield(lua, field, r.data.view(field->offset), r.settings);
		else
			lua_pushnil(lua);
		return 1;
	}
};

template <>
struct MetaT<reflection::Array> : public MetaBase
{
	static constexpr std::string_view name = "reflection_array";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Array>(lua, 1);
		auto key = luaL_checkinteger(lua, 2);

		return r.push(lua, key);
	}

	static int ipairs(lua_State* lua) { return pairs(lua); }
	static int pairs(lua_State* lua)
	{
		//auto & r = luaT_checkuserdata<reflection::Array>(lua, 1);
		lua_pushcfunction(lua, next);
		lua_pushvalue(lua, 1);
		lua_pushinteger(lua, 0);
		return 3;
	}

	static int next(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Array>(lua, 1);
		auto key = luaL_checkinteger(lua, 2) + 1;
		if (key > r.size(lua))
			return 0;
		lua_pushinteger(lua, key);
		return r.push(lua, key) + 1;
	}

	static int len(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Array>(lua, 1);

		lua_pushinteger(lua, r.size(lua));
		return 1;
	}
};

inline int reflection::Array::push(lua_State* lua, int key)
{
	auto idx = key - 1; // Lua counts from 1, not from zero
	if (field->type == tll::scheme::Field::Array) {
		auto size = tll::scheme::read_size(field->count_ptr, data.view(field->count_ptr->offset));
		if (size < 0)
			return luaL_error(lua, "Array %s has invalid size: %d", field->name, size);
		if (idx >= size)
			return luaL_error(lua, "Array %s index out of bounds (size %d): %d", field->name, size, key);
		auto f = field->type_array;
		if (data.size() < f->offset + f->size * f->count)
			return luaL_error(lua, "Array '%s' size %d > data size %d", field->name, f->offset + f->size * f->count, data.size());
		return pushfield(lua, f, data.view(f->offset + f->size * idx), settings);
	} else {
		auto ptr = tll::scheme::read_pointer(field, data);
		if (!ptr)
			return luaL_error(lua, "Unknown offset ptr version for %s: %d", field->name, field->offset_ptr_version);
		if ((size_t) idx >= ptr->size)
			return luaL_error(lua, "Array %s index out of bounds (size %d): %d", field->name, ptr->size, key);
		if (data.size() < ptr->offset + ptr->entity * ptr->size)
			return luaL_error(lua, "Array '%s' size %d > data size %d", field->name, ptr->offset + ptr->entity * ptr->size, data.size());
		return pushfield(lua, field->type_ptr, data.view(ptr->offset + ptr->entity * idx), settings);
	}
}

template <>
struct MetaT<reflection::Bits> : public MetaBase
{
	static constexpr std::string_view name = "reflection_bits";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Bits>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		auto bit = r.lookup(key);
		if (bit == nullptr)
			return luaL_error(lua, "Bits '%s' has no bit '%s'", r.field->name, key.data());

		auto bits = tll::scheme::read_size(r.field, r.data);
		auto v = tll_scheme_bit_field_get(bits, bit->offset, bit->size);
		if (bit->size == 1)
			lua_pushboolean(lua, v);
		else
			lua_pushinteger(lua, v);
		return 1;
	}

	static int init(lua_State* lua)
	{
		lua_pushcfunction(lua, band);
		lua_setfield(lua, -2, "__band");

		lua_pushcfunction(lua, bor);
		lua_setfield(lua, -2, "__bor");

		lua_pushcfunction(lua, bxor);
		lua_setfield(lua, -2, "__bxor");
		return 0;
	}

	template <typename Func>
	static int bfunc(lua_State *lua, Func f)
	{
		auto & r = *luaT_touserdata<reflection::Bits>(lua, 1);
		auto rhs = luaL_checkinteger(lua, 2);
		auto bits = tll::scheme::read_size(r.field, r.data);
		lua_pushinteger(lua, f(bits, rhs));
		return 1;
	}

	static int band(lua_State *lua) { return bfunc(lua, [](auto lhs, auto rhs) { return lhs & rhs; }); }
	static int bor(lua_State *lua) { return bfunc(lua, [](auto lhs, auto rhs) { return lhs | rhs; }); }
	static int bxor(lua_State *lua) { return bfunc(lua, [](auto lhs, auto rhs) { return lhs ^ rhs; }); }
};

template <>
struct MetaT<reflection::Decimal128> : public MetaBase
{
	static constexpr std::string_view name = "reflection_decimal128";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Decimal128>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "float") {
			tll::util::Decimal128::Unpacked u;
			r.data.unpack(u);
			if (u.exponent >= u.exp_inf) {
				if (u.isinf())
					lua_pushnumber(lua, std::numeric_limits<double>::infinity());
				else
					lua_pushnumber(lua, std::numeric_limits<double>::quiet_NaN());
			} else {
				long double v = u.mantissa.value;
				if (u.sign)
					v *= -1;
				v *= powl(10, u.exponent);
				lua_pushnumber(lua, v);
			}
		} else if (key == "string")
			luaT_pushstringview(lua, tll::conv::to_string(r.data));
		else
			lua_pushnil(lua);
		return 1;
	}

	static int tostring(lua_State *lua)
	{
		auto & r = *luaT_touserdata<reflection::Decimal128>(lua, 1);
		luaT_pushstringview(lua, tll::conv::to_string(r.data));
		return 1;
	}
};

template <>
struct MetaT<reflection::Fixed> : public MetaBase
{
	static constexpr std::string_view name = "reflection_fixed";
	static int index(lua_State* lua)
	{
		auto & self = *luaT_touserdata<reflection::Fixed>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "float") {
			double v = 0;
			using tll::scheme::Field;
			switch (self.field->type) {
			case Field::Int8:  v = *self.data.template dataT<int8_t>(); break;
			case Field::Int16: v = *self.data.template dataT<int16_t>(); break;
			case Field::Int32: v = *self.data.template dataT<int32_t>(); break;
			case Field::Int64: v = *self.data.template dataT<int64_t>(); break;
			case Field::UInt8:  v = *self.data.template dataT<uint8_t>(); break;
			case Field::UInt16: v = *self.data.template dataT<uint16_t>(); break;
			case Field::UInt32: v = *self.data.template dataT<uint32_t>(); break;
			case Field::UInt64: v = *self.data.template dataT<uint64_t>(); break;
			default:
				return luaL_error(lua, "Invalid type for Fixed field: %d", self.field->type);
			}
			lua_pushnumber(lua, v / intpow(10, self.field->fixed_precision));
		} else if (key == "string")
			luaT_pushstringview(lua, fmt::format("{}.E-{}", 0, self.field->fixed_precision));
		else
			lua_pushnil(lua);
		return 1;
	}

	static int tostring(lua_State *lua)
	{
		auto & r = *luaT_touserdata<reflection::Decimal128>(lua, 1);
		luaT_pushstringview(lua, tll::conv::to_string(r.data));
		return 1;
	}
};

template <>
struct MetaT<reflection::Enum> : public MetaBase
{
	static constexpr std::string_view name = "reflection_enum";

	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<reflection::Enum>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "int") {
			lua_pushnumber(lua, r.value);
		} else if (key == "string") {
			if (auto v = r.lookup(r.value); v)
				lua_pushstring(lua, v->name);
			else
				lua_pushnil(lua);
		} else if (key == "eq") {
			lua_pushcfunction(lua, eq);
		} else
			lua_pushnil(lua);
		return 1;
	}

	static int tostring(lua_State *lua)
	{
		auto & r = *luaT_touserdata<reflection::Enum>(lua, 1);
		if (auto v = r.lookup(r.value); v)
			lua_pushstring(lua, v->name);
		else
			luaT_pushstringview(lua, tll::conv::to_string(r.value));
		return 1;
	}

	static int eq(lua_State *lua)
	{
		auto & self = *luaT_touserdata<reflection::Enum>(lua, 1);
		if (lua_gettop(lua) != 2)
			return luaL_error(lua, "Invalid number of arguments to 'Enum::eq' function: expected 2, got {}", lua_gettop(lua));
		switch (auto type = lua_type(lua, 2); type) {
		case LUA_TNUMBER:
			lua_pushboolean(lua, self.value == lua_tointeger(lua, 2));
			break;
		case LUA_TSTRING:
			if (auto r = tll::scheme::lookup_name(self.desc->values, luaT_tostringview(lua, 2)); r)
				lua_pushboolean(lua, self.value == r->value);
			else
				lua_pushboolean(lua, 0);
			break;
		case LUA_TUSERDATA:
			if (auto r = luaT_touserdata<reflection::Enum>(lua, -1))
				lua_pushboolean(lua, self.value == r->value);
			else
				lua_pushboolean(lua, 0);
			break;
		default:
			lua_pushboolean(lua, 0);
			break;
		}
		return 1;
	}
};

} // namespace tll::lua

template <>
struct tll::conv::parse<tll::lua::Settings::Enum>
{
	using Enum = tll::lua::Settings::Enum;
        static result_t<Enum> to_any(std::string_view s)
        {
                return tll::conv::select(s, std::map<std::string_view, Enum> {
			{"int", Enum::Int},
			{"string", Enum::String},
			{"object", Enum::Object},
		});
        }
};

template <>
struct tll::conv::parse<tll::lua::Settings::Bits>
{
	using Bits = tll::lua::Settings::Bits;
        static result_t<Bits> to_any(std::string_view s)
        {
                return tll::conv::select(s, std::map<std::string_view, Bits> {
			{"int", Bits::Int},
			{"object", Bits::Object},
		});
        }
};

template <>
struct tll::conv::parse<tll::lua::Settings::Fixed>
{
	using Fixed = tll::lua::Settings::Fixed;
        static result_t<Fixed> to_any(std::string_view s)
        {
                return tll::conv::select(s, std::map<std::string_view, Fixed> {
			{"int", Fixed::Int},
			{"float", Fixed::Float},
			{"object", Fixed::Object},
		});
        }
};

#endif//_TLL_LUA_REFLECTION_H
