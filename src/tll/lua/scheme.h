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
			lua_pushinteger(lua, r.ptr->type);
		} else if (key == "type_enum") {
			if (r.ptr->sub_type == tll::scheme::Field::Enum)
				luaT_push(lua, scheme::Enum { r.ptr->type_enum });
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
			lua_pushinteger(lua, r.ptr->type);
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
