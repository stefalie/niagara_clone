#pragma once

#include <assert.h>

#include <volk.h>

// SHORTCUT: Would need to be checked properly in production.
#define VK_CHECK(call)            \
	do                            \
	{                             \
		const VkResult rc = call; \
		assert(rc == VK_SUCCESS); \
	} while (0)

#ifndef ARRAYSIZE
#define ARRAYSIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

