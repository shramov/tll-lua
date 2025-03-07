// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _TLL_LUA_TIME_H
#define _TLL_LUA_TIME_H

#include <tll/scheme.h>

#include "tll/lua/luat.h"

namespace tll::lua {

struct TimePoint
{
	tll_scheme_time_resolution_t resolution = TLL_SCHEME_TIME_NS;
	enum Type { Signed, Unsigned, Double } type = Signed;
	union {
		long long vsigned = 0;
		unsigned long long vunsigned;
		double vdouble;
	};

	double fvalue() const
	{
		switch (type) {
		case Signed: return vsigned;
		case Unsigned: return vunsigned;
		case Double: return vdouble;
		}
		return 0;
	}

	static constexpr std::pair<long long, long long> ratio(tll_scheme_time_resolution_t resolution)
	{
		switch (resolution) {
		case TLL_SCHEME_TIME_NS: return {1, 1000000000};
		case TLL_SCHEME_TIME_US: return {1, 1000000};
		case TLL_SCHEME_TIME_MS: return {1, 1000};
		case TLL_SCHEME_TIME_SECOND: return {1, 1};
		case TLL_SCHEME_TIME_MINUTE: return {60, 1};
		case TLL_SCHEME_TIME_HOUR: return {3600, 1};
		case TLL_SCHEME_TIME_DAY: return {86400, 1};
		}
		return {1, 1};
	}
	std::pair<long long, long long> ratio() const { return ratio(resolution); }

	template <typename T>
	T secondsT() const
	{
		auto [mul, div] = ratio();
		switch (type) {
		case Signed: return static_cast<T>(vsigned) * mul / div;
		case Unsigned: return static_cast<T>(vunsigned) * mul / div;
		case Double: return vdouble * mul / div;
		}
		return 0;
	}

	double fseconds() const { return secondsT<double>(); }
	long long seconds() const { return secondsT<long long>(); }

	unsigned ns() const
	{
		auto [_, div] = ratio();
		if (type == Double) {
			auto rem = fmod(vdouble, div);
			if (rem < 0)
				rem += div;
			return rem * (1000000000 / div);
		}
		long long rem = 0;
		if (type == Signed)
			rem = vsigned % div;
		else
			rem = vunsigned % div;
		return rem * (1000000000 / div);
	}

	const int unpack(struct tm &v)
	{
		time_t ts = seconds();

		if (!gmtime_r(&ts, &v))
			return ERANGE;
		return 0;
	}

	int pack(struct tm &v)
	{
		time_t ts = timegm(&v);
		if (ts == (time_t) -1)
			return EINVAL;
		auto [mul, div] = ratio();
		switch (type) {
		case Signed: vsigned = static_cast<long long>(ts) * div / mul; break;
		case Unsigned: vunsigned = static_cast<unsigned long long>(ts) * div / mul; break;
		case Double: vdouble = static_cast<double>(ts) * div / mul; break;
		}
		return 0;
	}

	int tostring(lua_State *lua)
	{
		struct tm v = {};
		if (unpack(v))
			return luaL_error(lua, "Timestamp overflow");
		if (resolution == TLL_SCHEME_TIME_DAY && type != Double) {
			char buf[10 + 1];
			strftime(buf, sizeof(buf), "%Y-%m-%d", &v);
			luaT_pushstringview(lua, buf);
			return 1;
		}
		char buf[10 + 1 + 8 + 1 + 9 + 1];
		auto off = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &v);
		if (auto ns = this->ns(); ns != 0) {
			if (ns % 1000000 == 0)
				snprintf(buf + off, sizeof(buf) - off, ".%03u", ns / 1000000);
			else if (ns % 1000 == 0)
				snprintf(buf + off, sizeof(buf) - off, ".%06u", ns / 1000);
			else
				snprintf(buf + off, sizeof(buf) - off, ".%09u", ns);
		}
		luaT_pushstringview(lua, buf);
		return 1;
	}

	static int create(lua_State * lua)
	{
		const auto args = lua_gettop(lua);
		struct tm v = {};
		long long ns = 0;
		if (args > 0) v.tm_year = lua_tonumber(lua, 1) - 1900;
		if (args > 1) v.tm_mon = lua_tonumber(lua, 2) - 1;
		if (args > 2) v.tm_mday = lua_tonumber(lua, 3);
		if (args > 3) v.tm_hour = lua_tonumber(lua, 4);
		if (args > 4) v.tm_min = lua_tonumber(lua, 5);
		if (args > 5) v.tm_sec = lua_tonumber(lua, 6);
		if (args > 6) ns = lua_tonumber(lua, 7);
		TimePoint r = {};
		r.type = Signed;
		if (r.pack(v))
			return luaL_error(lua, "Invalid time values");
		r.vsigned += ns;
		luaT_push<TimePoint>(lua, r);
		return 1;
	}

	auto compare (const TimePoint &rhs) const
	{
#ifdef __cpp_impl_three_way_comparison
		return fseconds() <=> rhs.fseconds();
#else
		const auto ls = fseconds(), rs = rhs.fseconds();
		if (ls == rs)
			return 0;
		else if (ls < rs)
			return -1;
		else
			return 1;
#endif
	}
};

template <>
struct MetaT<TimePoint> : public MetaBase
{
	static constexpr std::string_view name = "tll_time_point_t";
	static int index(lua_State* lua)
	{
		auto & self = *luaT_touserdata<TimePoint>(lua, 1);
		auto key = luaT_checkstringview(lua, 2);

		if (key == "date") {
			struct tm v = {};
			if (self.unpack(v))
				return luaL_error(lua, "Timestamp overflow");
			lua_pushnumber(lua, (1900 + v.tm_year) * 10000 + (v.tm_mon + 1) * 100 + v.tm_mday);
		} else if (key == "seconds") {
			lua_pushnumber(lua, self.fseconds());
		} else if (key == "string") {
			self.tostring(lua);
		} else
			lua_pushnil(lua);
		return 1;
	}

	static int tostring(lua_State *lua)
	{
		auto & self = *luaT_touserdata<TimePoint>(lua, 1);
		return self.tostring(lua);
	}

	static int eq(lua_State *lua)
	{
		auto & self = luaT_checkuserdata<TimePoint>(lua, 1);
		auto & rhs = luaT_checkuserdata<TimePoint>(lua, 2);
		return self.compare(rhs) == 0;
	}

	static int lt(lua_State *lua)
	{
		auto & self = luaT_checkuserdata<TimePoint>(lua, 1);
		auto & rhs = luaT_checkuserdata<TimePoint>(lua, 2);
		return self.compare(rhs) < 0;
	}

	static int le(lua_State *lua)
	{
		auto & self = luaT_checkuserdata<TimePoint>(lua, 1);
		auto & rhs = luaT_checkuserdata<TimePoint>(lua, 2);
		return self.compare(rhs) <= 0;
	}
};
} // namespace tll::lua
#endif//_TLL_LUA_TIME_H
