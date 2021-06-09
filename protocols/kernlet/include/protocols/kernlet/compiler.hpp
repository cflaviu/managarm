#pragma once

#include <async/result.hpp>
#include <helix/ipc.hpp>
#include <vector>

enum class BindType {
	null,
	offset,
	memoryView,
	bitsetEvent
};

async::result<void> connectKernletCompiler();
async::result<helix::UniqueDescriptor> compile(void *code, size_t size,
		std::vector<BindType> bind_types);
