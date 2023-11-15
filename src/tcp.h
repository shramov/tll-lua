#include <tll/channel/base.h>

class LuaTcp : public tll::channel::Base<LuaTcp>
{
 public:
	static constexpr std::string_view param_prefix() { return "tcp"; }
	static constexpr std::string_view channel_protocol() { return "tcp-lua"; }

	std::optional<const tll_channel_impl_t *> _init_replace(const tll::Channel::Url &url, tll::Channel *master);

	int _init(const tll::Channel::Url &url, tll::Channel * master) { return _log.fail(EINVAL, "Failed to choose proper tcp channel"); }
};
