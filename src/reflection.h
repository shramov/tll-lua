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
#include <tll/scheme.h>
#include <tll/scheme/types.h>
#include <tll/scheme/util.h>
#include <tll/util/memoryview.h>

namespace reflection {
struct Message
{
	const tll::scheme::Message * message = nullptr;
	tll::memoryview<const tll_msg_t> data;

	const tll::scheme::Field * lookup(std::string_view name) const
	{
		for (auto f = message->fields; f; f = f->next)
			if (f->name == name)
				return f;
		return nullptr;
	}
};

struct Array
{
	const tll::scheme::Field * field = nullptr;
	tll::memoryview<const tll_msg_t> data;
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
} // namespace reflection

namespace {
template <typename View, typename T>
int pushnumber(lua_State * lua, const tll::scheme::Field * field, View data, T v)
{
	if (field->sub_type == field->Bits)
		luaT_push<reflection::Bits>(lua, { field, data });
	else
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
int pushfield(lua_State * lua, const tll::scheme::Field * field, View data)
{
	using tll::scheme::Field;
	switch (field->type) {
	case Field::Int8:  return pushnumber(lua, field, data, *data.template dataT<int8_t>());
	case Field::Int16: return pushnumber(lua, field, data, *data.template dataT<int16_t>());
	case Field::Int32: return pushnumber(lua, field, data, *data.template dataT<int32_t>());
	case Field::Int64: return pushnumber(lua, field, data, *data.template dataT<int64_t>());
	case Field::UInt8:  return pushnumber(lua, field, data, *data.template dataT<uint8_t>());
	case Field::UInt16: return pushnumber(lua, field, data, *data.template dataT<uint16_t>());
	case Field::UInt32: return pushnumber(lua, field, data, *data.template dataT<uint32_t>());
	case Field::Double: return pushdouble(lua, field, data, *data.template dataT<double>());
	case Field::Decimal128:
		lua_pushnil(lua);
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
		luaT_push<reflection::Array>(lua, { field, data });
		break;
	case Field::Pointer:
		if (field->sub_type == Field::ByteString) {
			auto ptr = tll::scheme::read_pointer(field, data);
			if (!ptr)
				return luaL_error(lua, "Unknown offset ptr version for %s: %d", field->name, field->offset_ptr_version);
			lua_pushlstring(lua, data.view(ptr->offset).template dataT<const char>(), ptr->size);
		} else
			luaT_push<reflection::Array>(lua, { field, data });
		break;
	case Field::Message:
		luaT_push<reflection::Message>(lua, { field->type_msg, data });
		break;
	}
	return 1;
}
} // namespace

template <>
struct MetaT<reflection::Message> : public MetaBase
{
	static constexpr std::string_view name = "reflection_message";
	static int index(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Message>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (r.data.size() < r.message->size)
			return luaL_error(lua, "Message '%s' size %d > data size %d", r.message->name, r.message->size, r.data.size());
		auto field = r.lookup(key);
		if (field == nullptr)
			return luaL_error(lua, "Message '%s' has no field '%s'", r.message->name, key.data());

		return pushfield(lua, field, r.data.view(field->offset));
	}
};

template <>
struct MetaT<reflection::Array> : public MetaBase
{
	static constexpr std::string_view name = "reflection_array";
	static int index(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Array>(lua, 1, name);
		auto key = luaL_checkinteger(lua, 2);

		auto field = r.field;
		if (field->type == tll::scheme::Field::Array) {
			auto size = tll::scheme::read_size(field->count_ptr, r.data.view(field->count_ptr->offset));
			if (size < 0)
				return luaL_error(lua, "Array %s has invalid size: %d", field->name, size);
			if (key >= size)
				return luaL_error(lua, "Array %s index out of bounds (size %d): %d", field->name, size, key);
			auto f = r.field->type_array;
			if (r.data.size() < f->offset + f->size * f->count)
				return luaL_error(lua, "Array '%s' size %d > data size %d", field->name, f->offset + f->size * f->count, r.data.size());
			return pushfield(lua, f, r.data.view(f->offset + f->size * key));
		} else {
			auto ptr = tll::scheme::read_pointer(field, r.data);
			if (!ptr)
				return luaL_error(lua, "Unknown offset ptr version for %s: %d", field->name, field->offset_ptr_version);
			if ((size_t) key >= ptr->size)
				return luaL_error(lua, "Array %s index out of bounds (size %d): %d", field->name, ptr->size, key);
			if (r.data.size() < ptr->offset + ptr->entity * ptr->size)
				return luaL_error(lua, "Array '%s' size %d > data size %d", field->name, ptr->offset + ptr->entity * ptr->size, r.data.size());
			return pushfield(lua, r.field->type_ptr, r.data.view(ptr->offset + ptr->entity * key));
		}
	}
};

template <>
struct MetaT<reflection::Bits> : public MetaBase
{
	static constexpr std::string_view name = "reflection_bits";
	static int index(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Bits>(lua, 1);
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
};

#endif//_TLL_LUA_REFLECTION_H
