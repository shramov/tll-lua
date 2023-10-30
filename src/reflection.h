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
	struct Iterator
	{
		const Message * message = nullptr;
		const tll::scheme::Field * field = nullptr;
	};

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

struct Union
{
	const tll::scheme::Union * desc = nullptr;
	tll::memoryview<const tll_msg_t> data;
};

struct Array
{
	const tll::scheme::Field * field = nullptr;
	tll::memoryview<const tll_msg_t> data;

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
	case Field::UInt64: return pushnumber(lua, field, data, *data.template dataT<uint64_t>());
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
			lua_pushlstring(lua, data.view(ptr->offset).template dataT<const char>(), ptr->size ? ptr->size - 1 : 0);
		} else
			luaT_push<reflection::Array>(lua, { field, data });
		break;
	case Field::Message:
		luaT_push<reflection::Message>(lua, { field->type_msg, data });
		break;
	case Field::Union:
		luaT_push<reflection::Union>(lua, { field->type_union, data });
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
		auto & r = *luaT_touserdata<reflection::Message>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (r.data.size() < r.message->size)
			return luaL_error(lua, "Message '%s' size %d > data size %d", r.message->name, r.message->size, r.data.size());
		auto field = r.lookup(key);
		if (field == nullptr)
			return luaL_error(lua, "Message '%s' has no field '%s'", r.message->name, key.data());

		return pushfield(lua, field, r.data.view(field->offset));
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
		pushfield(lua, r.field, r.message->data.view(r.field->offset));
		r.field = r.field->next;
		return 2;
	}

	static int copy(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<reflection::Message>(lua, 1);
		lua_newtable(lua);
		for (auto f = r.message->fields; f; f = f->next) {
			luaT_pushstringview(lua, f->name);
			pushfield(lua, f, r.data.view(f->offset));
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
			pushfield(lua, field, r.data.view(field->offset));
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
		return pushfield(lua, f, data.view(f->offset + f->size * idx));
	} else {
		auto ptr = tll::scheme::read_pointer(field, data);
		if (!ptr)
			return luaL_error(lua, "Unknown offset ptr version for %s: %d", field->name, field->offset_ptr_version);
		if ((size_t) idx >= ptr->size)
			return luaL_error(lua, "Array %s index out of bounds (size %d): %d", field->name, ptr->size, key);
		if (data.size() < ptr->offset + ptr->entity * ptr->size)
			return luaL_error(lua, "Array '%s' size %d > data size %d", field->name, ptr->offset + ptr->entity * ptr->size, data.size());
		return pushfield(lua, field->type_ptr, data.view(ptr->offset + ptr->entity * idx));
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
};

#endif//_TLL_LUA_REFLECTION_H
