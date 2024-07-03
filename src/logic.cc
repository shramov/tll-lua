#include "logic.h"

#include "tll/lua/channel.h"

using namespace tll::lua;

int Logic::_init(const tll::Channel::Url &url, tll::Channel *master)
{
	return Base::_init(url, master);
}

int Logic::_open(const tll::ConstConfig &cfg)
{
	if (auto r = _lua_open(); r)
		return r;

	_functions.clear();
	lua_newtable(_lua);
	for (auto & [t, list] : _channels) {
		auto name = fmt::format("tll_on_channel_{}", t);
		lua_getglobal(_lua, name.c_str());
		if (!lua_isfunction(_lua, -1)) {
			name = "tll_on_channel";
			lua_pop(_lua, 1);
			lua_getglobal(_lua, name.c_str());
		}
		if (!lua_isfunction(_lua, -1))
			return _log.fail(EINVAL, "No callbacks for tag '{}': need either tll_on_channel_{} or tll_on_channel functions", t, t);
		lua_pop(_lua, 1);

		luaT_pushstringview(_lua, t);
		lua_newtable(_lua);
		auto idx = 0;
		for (auto &c : list) {
			_log.debug("Channel {} -> callback {}", c->name(), name);
			if (auto r = _functions.emplace(c, name); !r.second) {
				if (r.first->second != name)
					return _log.fail(EINVAL, "Channel {} has different callbacks: {} and {} from different tags", c->name(), name, r.first->second);
			}
			lua_pushinteger(_lua, ++idx);
			luaT_push<tll::lua::Channel>(_lua, { c, &_encoder });
			lua_settable(_lua, -3);
		}
		lua_settable(_lua, -3);
	}
	lua_setglobal(_lua, "tll_self_channels");

	luaT_push<tll::lua::Channel>(_lua, { self(), &_encoder });
	lua_setglobal(_lua, "tll_self");

	lua_getglobal(_lua, "tll_on_post");
	_with_on_post = lua_isfunction(_lua, -1);
	lua_pop(_lua, 1);

	if (auto r = _lua_on_open(cfg); r)
		return r;

	return Base::_open(cfg);
}

int Logic::logic(const tll::Channel * c, const tll_msg_t *msg)
{
	auto it = _functions.find(c);
	if (it == _functions.end())
		return _log.fail(EINVAL, "Channel {} is not found in function map", c->name());
	return _on_msg(msg, c->scheme(), it->first, it->second);
}

int Logic::_post(const tll_msg_t *msg, int flags)
{
	if (!_with_on_post)
		return Base::_post(msg, flags);
	if (_on_msg(msg, _scheme.get(), self(), "tll_on_post"))
		return EINVAL;
	return 0;
}

int Logic::_on_msg(const tll_msg_t *msg, const tll::Scheme * scheme, tll::Channel * channel, std::string_view func)
{
	auto ref = _lua.copy();
	lua_getglobal(ref, func.data());

	auto extra_args = 0;
	if (channel != self()) {
		luaT_push<tll::lua::Channel>(ref, { channel, &_encoder });
		extra_args++;
	}
	auto args = _lua_pushmsg(msg, scheme, channel);
	if (args < 0)
		return EINVAL;
	if (lua_pcall(ref, extra_args + args, 0, 0)) {
		auto text = fmt::format("Lua function {} failed: {}\n  on", func, lua_tostring(ref, -1));
		lua_pop(ref, 1);
		tll_channel_log_msg(channel, _log.name(), tll::logger::Error, _dump_error, msg, text.data(), text.size());
		return EINVAL;
	}

	return 0;
}
