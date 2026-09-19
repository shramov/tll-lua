#include "probe.h"

int Probe::_init(const tll::Channel::Url &cfg, tll::Channel * master)
{
	return Base::_init(cfg, master);
}

int Probe::_open(const tll::ConstConfig &cfg)
{
	if (auto r = _lua_open(); r)
		return r;

	_channels.clear();
	_child.reset();

	luaT_push<tll::lua::Channel>(_lua, { self(), &_encoder });
	lua_setglobal(_lua, "tll_self");

	lua_getglobal(_lua, "tll_probe_generate");
	if (!lua_isfunction(_lua, -1))
		return _log.fail(ENOENT, "Function {} not found", "tll_probe_generate");

	auto init = *_config.sub("init");
	luaT_push(_lua, tll::lua::Config { tll_config_ref(init) });
	if (lua_pcall(_lua, 1, 1, 0))
		return _log.fail(EINVAL, "Generator failed: {}", lua_tostring(_lua, -1));

	if (!lua_istable(_lua, -1))
		return _log.fail(EINVAL, "Generator returned non-table");

	lua_pushvalue(_lua, -1);
	lua_setglobal(_lua, "tll_probe_list");

	std::map<int, ProbePair> probes;

	lua_pushnil(_lua);
	while (lua_next(_lua, -2)) {
		lua_pushvalue(_lua, -2); // Copy key so it is not modified
		auto idx = lua_tointeger(_lua, -1);
		lua_pop(_lua, 1);
		_log.debug("Try to read probe {}", idx);

		if (auto r = _read_probe(); r)
			probes.emplace(idx, *r);
		else
			return _log.fail(EINVAL, "Failed to load probe {}: {}", idx, r.error());
		lua_pop(_lua, 1); // Remove value from stack
	}
	lua_pop(_lua, 1);

	lua_getglobal(_lua, "tll_probe_list");
	_channels.reserve(probes.size());
	for (auto &[i, p] : probes) {
		auto c = Channel { .index = i, .open = std::move(p.open), .parent = this };
		auto curl = _child_url.copy();
		curl.merge(p.init);
		child_url_fill(curl, fmt::format("probe-{}", i));
		c.channel = context().channel(curl);
		if (!c.channel)
			return _log.fail(EINVAL, "Failed to create child channel");
		_child_add(c.channel.get());

		lua_pushinteger(_lua, c.index);
		if (lua_gettable(_lua, -2) == LUA_TNIL)
			return _log.fail(EINVAL, "Failed to get probe object at {}", c.index);

		lua_pushstring(_lua, "channel");
		luaT_push<tll::lua::Channel>(_lua, { c.channel.get(), &_encoder });
		lua_settable(_lua, -3);

		lua_pushstring(_lua, "index");
		lua_pushinteger(_lua, c.index);
		lua_settable(_lua, -3);

		lua_pop(_lua, 1);

		_channels.emplace_back(std::move(c));
	}
	lua_pop(_lua, 1); // Remove probe list

	for (auto &c : _channels) {
		c.channel->callback_add<Channel, &Channel::callback>(&c, TLL_MESSAGE_MASK_ALL);
		if (auto r = c.channel->open(c.open); r)
			return _log.fail(EINVAL, "Failed to open child");
	}

	return 0;
}

tll::result_t<Probe::ProbePair> Probe::_read_probe()
{
	lua_pushstring(_lua, "init");
	lua_gettable(_lua, -2);
	auto init = _read_config();
	if (!init)
		return tll::error(fmt::format("Failed to load init config: {}", init.error()));
	lua_pop(_lua, 1);

	lua_pushstring(_lua, "open");
	lua_gettable(_lua, -2);
	auto open = _read_config();
	if (!open)
		return tll::error(fmt::format("Failed to load open config: {}", open.error()));
	lua_pop(_lua, 1);

	return ProbePair{*init, *open};
}

tll::result_t<tll::Config> Probe::_read_config()
{
	tll::Config cfg;

	if (!lua_istable(_lua, -1))
		return tll::error("Can not convert non-table to config");

	lua_pushnil(_lua);

	while (lua_next(_lua, -2)) {
		lua_pushvalue(_lua, -2); // Copy key so it is not modified
		auto key = luaT_tostringview(_lua, -1);
		auto value = luaT_tostringview(_lua, -2);
		if (auto r = cfg.set(key, value); r)
			return tll::error(fmt::format("Failed to set key {}: {}", key, strerror(r)));
		lua_pop(_lua, 2); // Drop key copy and value
	}

	return cfg;
}

int Probe::Channel::on_msg(const tll_msg_t * msg)
{
	if (state != tll::state::Opening)
		return 0;
	if (msg->type == TLL_MESSAGE_STATE) {
		switch ((tll_state_t) msg->msgid) {
		case tll::state::Active: // Only Active is passed to the script
			return call(stage, msg);
		case tll::state::Error:
			return parent->_log.fail(EINVAL, "Child {} failed", index);
		case tll::state::Closing:
			return parent->_log.fail(EINVAL, "Child {} is closing", index);
		default:
			return 0;
		}
	}
	return call(stage, msg);
}

int Probe::Channel::call(std::string_view stage, const tll_msg_t *msg)
{
	auto lua = parent->_lua.copy();
	auto guard = tll::lua::StackGuard(lua);

	lua_getglobal(lua, "tll_probe_list");
	lua_pushinteger(lua, index);
	lua_gettable(lua, -2);
	auto func = fmt::format("on_{}", stage);
	luaT_pushstringview(lua, func);
	if (lua_gettable(lua, -2) != LUA_TFUNCTION)
		return parent->_log.fail(EINVAL, "Function {} not found", func);
	lua_rotate(lua, -2, 1); // First argument - probe

	auto args = parent->_lua_pushmsg(msg, channel->scheme(), channel.get());
	if (args < 0)
		return parent->_log.fail(EINVAL, "Failed to push message");
	args += 1; // Probe

	if (lua_pcall(lua, args, 1, 0)) {
		auto text = fmt::format("Lua function {} failed: {}\n  on", func, lua_tostring(lua, -1));
		tll_channel_log_msg(channel.get(), parent->_log.name(), tll::logger::Error, TLL_MESSAGE_LOG_FRAME, msg, text.data(), text.size());
		return EINVAL;
	}

	switch (lua_type(lua, -1)) {
	case LUA_TSTRING:
		break;
	case LUA_TNIL:
		return 0;
	default:
		parent->_log.warning("Non-string return value from {}", func);
		return 0;
	}
	auto s = luaT_tostringview(lua, -1);
	parent->_log.info("Stage change: {} -> {}", this->stage, s);
	this->stage = s;
	if (s == "done")
		state = tll::state::Active;

	parent->_check_ready();
	return 0;
}

int Probe::_process()
{
	_update_dcaps(0, tll::dcaps::Process | tll::dcaps::Pending);
	if (std::any_of(_channels.begin(), _channels.end(), [](auto &c) { return c.state == tll::state::Opening; })) {
		_log.warning("Not all probes finished");
		return EAGAIN;
	}
	if (std::all_of(_channels.begin(), _channels.end(), [](auto &c) { return c.state == tll::state::Error; }))
		return _log.fail(EINVAL, "All probes failed, nothing to select");

	auto probe = _channels.end();
	lua_getglobal(_lua, "tll_probe_select");
	if (lua_isfunction(_lua, -1)) {
		lua_getglobal(_lua, "tll_probe_list");
		for (auto &c : _channels) {
			if (c.state != tll::state::Error)
				continue;
			_log.debug("Remove failed probe {}", c.index);
			lua_pushinteger(_lua, c.index);
			lua_pushnil(_lua);
			lua_settable(_lua, -3);
		}
		if (lua_pcall(_lua, 1, 1, 0))
			return _log.fail(EINVAL, "Select failed: {}", lua_tostring(_lua, -1));
		if (lua_type(_lua, -1) != LUA_TNUMBER) {
			auto s = lua_tostring(_lua, -1);
			return _log.fail(EINVAL, "Invalid select result: {}", s ? s : "nil");
		}
		auto index = lua_tointeger(_lua, -1);
		_log.info("Selected probe {}", index);
		probe = std::find_if(_channels.begin(), _channels.end(), [index](auto &c) { return c.index == index; });
		if (probe == _channels.end())
			return _log.fail(EINVAL, "Probe {} not found", index);
	} else {
		_log.debug("No tll_probe_select function, get metric from probes");
		for (auto c = _channels.begin(); c != _channels.end(); c++) {
			if (c->state == tll::state::Error)
				continue;
			auto guard = tll::lua::StackGuard(_lua);
			lua_getglobal(_lua, "tll_probe_list");
			lua_pushinteger(_lua, c->index);
			if (lua_gettable(_lua, -2) == LUA_TNIL)
				return _log.fail(EINVAL, "Failed to get probe object at {}", c->index);
			luaT_pushstringview(_lua, "metric");
			switch (lua_gettable(_lua, -2)) {
			case LUA_TNUMBER:
				c->metric = lua_tonumber(_lua, -1);
				break;
			case LUA_TFUNCTION:
				lua_rotate(_lua, -2, 1); // First argument - probe
				if (lua_pcall(_lua, 1, 1, 0))
					return _log.fail(EINVAL, "Probe.metric for {} failed: {}", c->index, lua_tostring(_lua, -1));
				if (!lua_isnumber(_lua, -1))
					return _log.fail(EINVAL, "Probe.metric for {} returned non-number", c->index);
				c->metric = lua_tonumber(_lua, -1);
				break;
			default:
				return _log.fail(EINVAL, "Probe {} metric field is not number of function", c->index);
			}

			if (probe == _channels.end()) {
				probe = c;
				continue;
			}
			if (probe->metric < c->metric)
				probe = c;
		}
		if (probe == _channels.end())
			return _log.fail(EINVAL, "No probe selected");
	}
	_child = std::move(probe->channel);
	_child->callback_del<Channel, &Channel::callback>(&*probe, TLL_MESSAGE_MASK_ALL);
	_child->callback_add(this, TLL_MESSAGE_MASK_ALL);

	_channels.clear();
	return Base::_on_active();
}
