// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _TLL_LUA_SCHEME_H
#define _TLL_LUA_SCHEME_H

#include <tll/lua/luat.h>

#include <tll/scheme.h>

namespace tll::lua {

namespace scheme {
struct Scheme
{
	const tll::Scheme * ptr;
};

struct Message
{
	const tll::scheme::Message * ptr;
};

struct Field
{
	const tll::scheme::Field * ptr;
};

struct Options
{
	const tll::scheme::Option * ptr;
};

struct Enum
{
	const tll::scheme::Enum * ptr;
};

struct Bits
{
	const tll::scheme::BitFields * ptr;
};

inline std::string_view format_as(tll_scheme_field_type_t t)
{
	using tll::scheme::Field;
	switch(t) {
	case Field::Int8: return "int8";
	case Field::Int16: return "int16";
	case Field::Int32: return "int32";
	case Field::Int64: return "int64";
	case Field::UInt8: return "uint8";
	case Field::UInt16: return "uint16";
	case Field::UInt32: return "uint32";
	case Field::UInt64: return "uint64";
	case Field::Double: return "double";
	case Field::Decimal128: return "decimal128";
	case Field::Bytes: return "bytes";
	case Field::Array: return "array";
	case Field::Pointer: return "pointer";
	case Field::Message: return "message";
	case Field::Union: return "union";
	}
	return "undefined";
}
} // namespace scheme

template <>
struct MetaT<scheme::Scheme> : public MetaBase
{
	static constexpr std::string_view name = "tll_scheme_scheme";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Scheme>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "options") {
			luaT_push(lua, scheme::Options { r.ptr->options });
		} else if (key == "messages") {
			lua_newtable(lua);
			for (auto i = r.ptr->messages; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				luaT_push(lua, scheme::Message { i });
				lua_settable(lua, -3);
			}
		} else if (key == "enums") {
			lua_newtable(lua);
			for (auto i = r.ptr->enums; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				luaT_push(lua, scheme::Enum { i });
				lua_settable(lua, -3);
			}
		} else if (key == "bits") {
			lua_newtable(lua);
			for (auto i = r.ptr->bits; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				luaT_push(lua, scheme::Bits { i });
				lua_settable(lua, -3);
			}
		} else
			return luaL_error(lua, "Invalid scheme::Scheme attribute '%s'", key.data());
		return 1;
	}

	static int pairs(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Scheme>(lua, 1);
		lua_pushcfunction(lua, next);
		luaT_push(lua, scheme::Message { r.ptr->messages });
		lua_pushnil(lua);
		return 3;
	}

	static int next(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<scheme::Message>(lua, 1);
		if (!r.ptr)
			return 0;
		lua_pushstring(lua, r.ptr->name);
		luaT_push(lua, scheme::Message { r.ptr });
		r.ptr = r.ptr->next;
		return 2;
	}
};

template <>
struct MetaT<scheme::Message> : public MetaBase
{
	static constexpr std::string_view name = "tll_scheme_message";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Message>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "options") {
			luaT_push(lua, scheme::Options { r.ptr->options });
		} else if (key == "name") {
			lua_pushstring(lua, r.ptr->name);
		} else if (key == "fields") {
			lua_newtable(lua);
			for (auto i = r.ptr->fields; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				luaT_push(lua, scheme::Field { i });
				lua_settable(lua, -3);
			}
		} else if (key == "enums") {
			lua_newtable(lua);
			for (auto i = r.ptr->enums; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				luaT_push(lua, scheme::Enum { i });
				lua_settable(lua, -3);
			}
		} else if (key == "bits") {
			lua_newtable(lua);
			for (auto i = r.ptr->bits; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				luaT_push(lua, scheme::Bits { i });
				lua_settable(lua, -3);
			}
		} else
			return luaL_error(lua, "Invalid scheme::Message attribute '%s'", key.data());

		return 1;
	}
};

template <>
struct MetaT<scheme::Field> : public MetaBase
{
	static constexpr std::string_view name = "tll_scheme_field";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Field>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "options") {
			luaT_push(lua, scheme::Options { r.ptr->options });
		} else if (key == "name") {
			lua_pushstring(lua, r.ptr->name);
		} else if (key == "type") {
			luaT_pushstringview(lua, scheme::format_as(r.ptr->type));
		} else if (key == "type_enum") {
			if (r.ptr->sub_type == tll::scheme::Field::Enum)
				luaT_push(lua, scheme::Enum { r.ptr->type_enum });
			else
				lua_pushnil(lua);
		} else if (key == "type_bits") {
			if (r.ptr->sub_type == tll::scheme::Field::Bits)
				luaT_push(lua, scheme::Bits { r.ptr->type_bits });
			else
				lua_pushnil(lua);
		} else
			return luaL_error(lua, "Invalid Field attribute '%s'", key.data());

		return 1;
	}
};

template <>
struct MetaT<scheme::Enum> : public MetaBase
{
	static constexpr std::string_view name = "tll_scheme_enum";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Enum>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "options") {
			luaT_push(lua, scheme::Options { r.ptr->options });
		} else if (key == "name") {
			lua_pushstring(lua, r.ptr->name);
		} else if (key == "type") {
			luaT_pushstringview(lua, scheme::format_as(r.ptr->type));
		} else if (key == "values") {
			lua_newtable(lua);
			for (auto i = r.ptr->values; i; i = i->next) {
				luaT_pushstringview(lua, i->name);
				lua_pushinteger(lua, i->value);
				lua_settable(lua, -3);
			}
		} else
			return luaL_error(lua, "Invalid Enum attribute '%s'", key.data());

		return 1;
	}
};

template <>
struct MetaT<scheme::Bits> : public MetaBase
{
	static constexpr std::string_view name = "tll_scheme_bits";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Bits>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "options") {
			luaT_push(lua, scheme::Options { r.ptr->options });
		} else if (key == "name") {
			lua_pushstring(lua, r.ptr->name);
		} else if (key == "type") {
			luaT_pushstringview(lua, scheme::format_as(r.ptr->type));
		} else if (key == "values") {
			lua_newtable(lua);
			for (auto i = r.ptr->values; i; i = i->next) {
				luaT_pushstringview(lua, i->name);

				lua_newtable(lua);
				luaT_pushstringview(lua, "name");
				lua_pushstring(lua, i->name);
				lua_settable(lua, -3);

				luaT_pushstringview(lua, "offset");
				lua_pushinteger(lua, i->offset);
				lua_settable(lua, -3);

				luaT_pushstringview(lua, "size");
				lua_pushinteger(lua, i->size);
				lua_settable(lua, -3);

				luaT_pushstringview(lua, "value");
				long long value = 1 << i->size;
				lua_pushinteger(lua, (value - 1) << i->offset);
				lua_settable(lua, -3);

				lua_settable(lua, -3);
			}
		} else
			return luaL_error(lua, "Invalid Bits attribute '%s'", key.data());

		return 1;
	}
};

template <>
struct MetaT<scheme::Options> : public MetaBase
{
	static constexpr std::string_view name = "tll_scheme_options";
	static int index(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Options>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (auto o = tll::scheme::lookup_name(r.ptr, key); o)
			lua_pushstring(lua, o->value);
		else
			lua_pushnil(lua);
		return 1;
	}

	static int pairs(lua_State* lua)
	{
		auto & r = *luaT_touserdata<scheme::Options>(lua, 1);
		lua_pushcfunction(lua, next);
		luaT_push(lua, scheme::Options { r.ptr });
		lua_pushnil(lua);
		return 3;
	}

	static int next(lua_State* lua)
	{
		auto & r = luaT_checkuserdata<scheme::Options>(lua, 1);
		if (!r.ptr)
			return 0;
		lua_pushstring(lua, r.ptr->name);
		lua_pushstring(lua, r.ptr->value);
		r.ptr = r.ptr->next;
		return 2;
	}
};

} // namespace tll::lua

#endif//_TLL_LUA_SCHEME_H
