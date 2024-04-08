/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_LUA_LUAT_H
#define _TLL_LUA_LUAT_H

#include <lua.hpp>

#include <memory>
#include <string_view>

namespace tll::lua {

using unique_lua_ptr_t = std::unique_ptr<lua_State, decltype(&lua_close)>;

struct MetaBase
{
	static constexpr void * index = nullptr;
	static constexpr void * newindex = nullptr;
	static constexpr void * pairs = nullptr;
	static constexpr void * ipairs = nullptr;
	static constexpr void * len = nullptr;
	static constexpr void * gc = nullptr;
	static constexpr void * tostring = nullptr;
	static constexpr void * init = nullptr;
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
		if constexpr (MetaT<T>::pairs != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::pairs);
			lua_setfield(lua, -2, "__pairs");
		}
		if constexpr (MetaT<T>::ipairs != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::ipairs);
			lua_setfield(lua, -2, "__ipairs");
		}
		if constexpr (MetaT<T>::len != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::len);
			lua_setfield(lua, -2, "__len");
		}
		if constexpr (MetaT<T>::gc != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::gc);
			lua_setfield(lua, -2, "__gc");
		}
		if constexpr (MetaT<T>::tostring != nullptr) {
			lua_pushcfunction(lua, MetaT<T>::tostring);
			lua_setfield(lua, -2, "__tostring");
		}
		if constexpr (MetaT<T>::init != nullptr)
			MetaT<T>::init(lua);
		lua_pop(lua, 1);
		return 0;
	}

	static int push(lua_State* lua, T && value)
	{
		new(lua_newuserdata(lua, sizeof(T))) T(std::move(value));
		luaL_setmetatable(lua, MetaT<T>::name.data());
		return 0;
	}
};

} // namespace tll::lua

template <typename T>
int luaT_push(lua_State * lua, T value) { return tll::lua::LuaT<T>::push(lua, std::move(value)); }

template <typename T>
T * luaT_touserdata(lua_State * lua, int index)
{
	return (T *) lua_touserdata(lua, index);
}

template <typename T>
T * luaT_testudata(lua_State * lua, int index, std::string_view tag)
{
	return (T *) luaL_testudata(lua, index, tag.data());
}

template <typename T>
T * luaT_testudata(lua_State * lua, int index)
{
	return luaT_testudata<T>(lua, index, tll::lua::MetaT<T>::name.data());
}

template <typename T>
T & luaT_checkuserdata(lua_State * lua, int index, std::string_view tag)
{
	return * (T *) luaL_checkudata(lua, index, tag.data());
}

template <typename T>
T & luaT_checkuserdata(lua_State * lua, int index)
{
	return luaT_checkuserdata<T>(lua, index, tll::lua::MetaT<T>::name.data());
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

#endif//_TLL_LUA_LUAT_H
