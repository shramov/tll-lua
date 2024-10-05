#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <lua.hpp>

#include <tll/channel/module.h>

#include "logic.h"
#include "measure.h"
#include "prefix.h"
#include "tcp.h"

TLL_DEFINE_IMPL(LuaTcp);
TLL_DEFINE_IMPL(LuaPrefix);
TLL_DEFINE_IMPL(tll::lua::LuaMeasure);
TLL_DEFINE_IMPL(tll::lua::Logic);

static int luainit(struct tll_channel_module_t * m, tll_channel_context_t * ctx, const tll_config_t * cfg)
{
	Dl_info info = {};
	tll::Logger log = { "tll.module.lua" };
	if (!dladdr((const void *) lua_newstate, &info))
		return log.fail(EINVAL, "Failed to get dlinfo of python library: {}", dlerror());

	log.debug("Reload Lua with RTLD_GLOBAL: {}", info.dli_fname);
	if (!dlopen(info.dli_fname, RTLD_GLOBAL | RTLD_NOLOAD | RTLD_NOW)) {
		return log.fail(EINVAL, "Failed to reload {} with RTLD_GLOBAL: {}", info.dli_fname, dlerror());
	}
	return 0;
}

static tll_channel_impl_t *channels[] = {
	&LuaTcp::impl,
	&LuaPrefix::impl,
	&tll::lua::LuaMeasure::impl,
	&tll::lua::Logic::impl,
	nullptr
};

static tll_channel_module_t mod = {
	.version = TLL_CHANNEL_MODULE_VERSION,
	.impl = channels,
	.init = luainit,
};

extern "C" tll_channel_module_t * tll_channel_module()
{
	return &mod;
}
