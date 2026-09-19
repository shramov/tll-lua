#ifndef _PROBE_H
#define _PROBE_H

#include <tll/channel/prefix.h>
#include <tll/lua/base.h>

class Probe : public tll::lua::LuaBase<Probe, tll::channel::Prefix<Probe>>
{
	using Base = tll::lua::LuaBase<Probe, tll::channel::Prefix<Probe>>;

	struct ProbePair
	{
		tll::Config init;
		tll::Config open;
	};

	struct Channel
	{
		int index = 0;
		std::unique_ptr<tll::Channel> channel;
		tll::Config open;
		Probe * parent = nullptr;
		std::string stage = "active";
		tll_state_t state = tll::state::Opening;
		double metric = 0;

		int callback(const tll::Channel *, const tll_msg_t *m)
		{
			if (on_msg(m)) {
				state = tll::state::Error;
				parent->_child_del(channel.get());
				parent->_check_ready();
			}
			return 0;
		}
		int on_msg(const tll_msg_t *m);
		int call(std::string_view stage, const tll_msg_t *m);
	};

	std::vector<Channel> _channels;
	tll::Channel::Url _child_url;

 public:
	static constexpr std::string_view channel_protocol() { return "lua-probe+"; }
	static constexpr auto prefix_child_policy() { return PrefixChildPolicy::Manual; }
	static constexpr auto process_api_version() { return ProcessAPI::Void; }
	static constexpr auto lua_close_policy() { return LuaClosePolicy::Skip; }

	int _init(const tll::Channel::Url &cfg, tll::Channel * master);
	int _open(const tll::ConstConfig &cfg);

	int _close(bool force = false)
	{
		_channels.clear();
		_lua.reset();
		return Base::_close(force);
	}

	int _on_init(tll::Channel::Url &curl, const tll::Channel::Url &, const tll::Channel *)
	{
		_child_url = curl.copy();
		return 0;
	}

	int _on_closed()
	{
		_channels.clear();
		_lua.reset();
		return Base::_on_closed();
	}

	int _post(const tll_msg_t *msg, int flags)
	{
		if (state() != tll::state::Active)
			return _log.fail(EINVAL, "Post in invalid state");
		return _child->post(msg, flags);
	}

	int _process();
 private:
	tll::result_t<ProbePair> _read_probe();
	tll::result_t<tll::Config> _read_config();

	void _check_ready()
	{
		if (!std::any_of(_channels.begin(), _channels.end(), [](auto &c) { return c.state == tll::state::Opening; })) {
			_log.info("All probes finished");
			_update_dcaps(tll::dcaps::Process | tll::dcaps::Pending);
		}
	}
};

#endif//_PROBE_H
