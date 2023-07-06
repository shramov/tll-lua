/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_T_H
#define _TLL_LUA_T_H

#include <lua.hpp>

#include <memory>
#include <string_view>

using unique_lua_ptr_t = std::unique_ptr<lua_State, decltype(&lua_close)>;

struct MetaBase
{
	static constexpr void * index = nullptr;
	static constexpr void * newindex = nullptr;
};

template <typename T>
struct MetaT {};

template <typename T>
struct LuaT
{
	static int init(lua_State* lua)
	{
		luaL_newmetatable(lua, MetaT<T>::name.data());
		if constexpr (MetaT<T>::newindex != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::newindex);
			lua_setfield(lua, -2, "__newindex");
		}
		if constexpr (MetaT<T>::index != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::index);
			lua_setfield(lua, -2, "__index");
		}
		lua_pop(lua, 1);
		return 0;
	}

	static int push(lua_State* lua, const T &ptr)
	{
		*(T *)lua_newuserdata(lua, sizeof(T)) = ptr;
		luaL_setmetatable(lua, MetaT<T>::name.data());
		return 0;
	}
};

template <typename T>
int luaT_push(lua_State * lua, const T & value) { return LuaT<T>::push(lua, value); }

template <typename T>
T * luaT_testuserdata(lua_State * lua, int index, std::string_view tag)
{
	return (T *) luaL_testudata(lua, index, tag.data());
}

template <typename T>
T * luaT_testuserdata(lua_State * lua, int index)
{
	return luaT_testuserdata<T>(lua, index, MetaT<T>::name.data());
}

template <typename T>
T & luaT_checkuserdata(lua_State * lua, int index, std::string_view tag)
{
	return * (T *) luaL_checkudata(lua, index, tag.data());
}

template <typename T>
T & luaT_checkuserdata(lua_State * lua, int index)
{
	return luaT_checkuserdata<T>(lua, index, MetaT<T>::name.data());
}

inline std::string_view luaT_checkstringview(lua_State * lua, int index)
{
	size_t size = 0;
	auto s = luaL_checklstring(lua, index, &size);
	return {s, size};
}

inline std::string_view luaT_tostringview(lua_State * lua, int index)
{
	size_t size = 0;
	auto s = lua_tolstring(lua, index, &size);
	return {s, size};
}

inline const char * luaT_pushstringview(lua_State * lua, std::string_view s) { return lua_pushlstring(lua, s.data(), s.size()); }

#endif//_TLL_LUA_T_H
