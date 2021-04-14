#pragma once

#include <assert.h>

#include <volk.h>

#include <vector>

// SHORTCUT: Would need to be checked properly in production.
#define VK_CHECK(call)            \
	do                            \
	{                             \
		const VkResult rc = call; \
		assert(rc == VK_SUCCESS); \
	} while (0)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

