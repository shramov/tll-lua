// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _LUA_LOGIC_H
#define _LUA_LOGIC_H

#include "tll/lua/base.h"

#include <tll/channel/logic.h>

#include <map>

namespace tll::lua {

class Logic : public tll::lua::LuaBase<Logic, tll::channel::Logic<Logic>>
{
	using Base = tll::lua::LuaBase<Logic, tll::channel::Logic<Logic>>;

	bool _with_on_post;
	std::map<tll::Channel *, std::string, std::less<>> _functions;

 public:
	static constexpr std::string_view channel_protocol() { return "lua"; }

	int _init(const tll::Channel::Url &url, tll::Channel *master);
	int _open(const tll::ConstConfig &cfg);
	int logic(const tll::Channel * c, const tll_msg_t *msg);

	int _post(const tll_msg_t *msg, int flags);

	int _on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, tll::Channel * c, std::string_view func);
};

} // namespace tll::lua

#endif//_LUA_LOGIC_H
