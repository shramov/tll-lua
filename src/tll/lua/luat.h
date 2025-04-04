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

class LuaRc {
	lua_State * _ptr = nullptr;
	int * _ref = nullptr;

 public:
	LuaRc() = default;
	LuaRc(lua_State * lua) : _ptr(nullptr), _ref(nullptr) { reset(lua); }
	LuaRc(LuaRc &ptr) noexcept : _ptr(ptr._ptr), _ref(ptr._ref) { if (_ptr) ++*_ref; }
	LuaRc(LuaRc &&ptr) noexcept : _ptr(nullptr), _ref(nullptr) { swap(ptr); }
	~LuaRc() { reset(); }

	void swap(LuaRc &rhs) noexcept { std::swap(_ptr, rhs._ptr); std::swap(_ref, rhs._ref); }

	LuaRc & operator = (LuaRc rhs) noexcept { swap(rhs); return *this; }

	LuaRc copy() noexcept { return LuaRc(*this); }

	void reset(lua_State * lua = nullptr)
	{
		if (_ptr) {
			if (--*_ref == 0) {
				lua_close(_ptr);
				delete _ref;
			}
			_ptr = nullptr;
			_ref = nullptr;
		}
		if (lua) {
			_ptr = lua;
			_ref = new int { 1 };
		}
	}

	constexpr lua_State * get() noexcept { return _ptr; }
	constexpr operator lua_State * () noexcept { return get(); }
	constexpr operator bool () const noexcept { return _ptr != nullptr; }
};

struct StackGuard
{
	lua_State * _lua = nullptr;
	int _top = 0;

	StackGuard(lua_State * lua) : _lua(lua), _top(lua_gettop(_lua)) {}

	~StackGuard()
	{
		if (!_lua)
			return;
		if (auto top = lua_gettop(_lua); top > _top)
			lua_pop(_lua, top - _top);
	}

	void release()
	{
		_lua = nullptr;
	}
};

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
	static constexpr void * eq = nullptr;
	static constexpr void * le = nullptr;
	static constexpr void * lt = nullptr;
};

template <typename T>
struct MetaT {};

template <typename T>
struct LuaT
{
	static int init(lua_State* lua)
	{
		luaL_newmetatable(lua, MetaT<T>::name.data());
		if constexpr (std::is_function_v<decltype(MetaT<T>::newindex)>) {
			lua_pushcfunction(lua, MetaT<T>::newindex);
			lua_setfield(lua, -2, "__newindex");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::index)>) {
			lua_pushcfunction(lua, MetaT<T>::index);
			lua_setfield(lua, -2, "__index");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::pairs)>) {
			lua_pushcfunction(lua, MetaT<T>::pairs);
			lua_setfield(lua, -2, "__pairs");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::ipairs)>) {
			lua_pushcfunction(lua, MetaT<T>::ipairs);
			lua_setfield(lua, -2, "__ipairs");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::len)>) {
			lua_pushcfunction(lua, MetaT<T>::len);
			lua_setfield(lua, -2, "__len");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::gc)>) {
			lua_pushcfunction(lua, MetaT<T>::gc);
			lua_setfield(lua, -2, "__gc");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::tostring)>) {
			lua_pushcfunction(lua, MetaT<T>::tostring);
			lua_setfield(lua, -2, "__tostring");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::eq)>) {
			lua_pushcfunction(lua, MetaT<T>::eq);
			lua_setfield(lua, -2, "__eq");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::lt)>) {
			lua_pushcfunction(lua, MetaT<T>::lt);
			lua_setfield(lua, -2, "__lt");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::le)>) {
			lua_pushcfunction(lua, MetaT<T>::le);
			lua_setfield(lua, -2, "__le");
		}
		if constexpr (std::is_function_v<decltype(MetaT<T>::init)>)
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
