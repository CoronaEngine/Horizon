
#include "vulkan_debug.h"

namespace ocarina {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
	VkDebugUtilsMessengerEXT debug_utils_messenger;

	VKAPI_ATTR VkBool32 VKAPI_CALL debug_message_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_type,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data)
	{
		// Select prefix depending on flags passed to the callback
		std::string prefix;

		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
			prefix = "VERBOSE: ";
#if defined(_WIN32)
			prefix = "\033[32m" + prefix + "\033[0m";
#endif
		}
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
			prefix = "INFO: ";
#if defined(_WIN32)
			prefix = "\033[36m" + prefix + "\033[0m";
#endif
		}
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
			prefix = "WARNING: ";
#if defined(_WIN32)
			prefix = "\033[33m" + prefix + "\033[0m";
#endif
		}
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			prefix = "ERROR: ";
#if defined(_WIN32)
			prefix = "\033[31m" + prefix + "\033[0m";
#endif
		}


		// Display message to default output (console/logcat)
		std::stringstream debug_message;
		if (callback_data->pMessageIdName) {
			debug_message << prefix << "[" << callback_data->messageIdNumber << "][" << callback_data->pMessageIdName << "] : " << callback_data->pMessage;
		}
		else {
			debug_message << prefix << "[" << callback_data->messageIdNumber << "] : " << callback_data->pMessage;
		}

#if defined(__ANDROID__)
		if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			LOGE("%s", debug_message.str().c_str());
		}
		else {
			LOGD("%s", debug_message.str().c_str());
		}
#else
		if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			std::cerr << debug_message.str() << "\n\n";
		}
		else {
			std::cout << debug_message.str() << "\n\n";
		}
		fflush(stdout);
#endif


		// The return value of this callback controls whether the Vulkan call that caused the validation message will be aborted or not
		// We return VK_FALSE as we DON'T want Vulkan calls that cause a validation message to abort
		// If you instead want to have calls abort, pass in VK_TRUE and the function will return VK_ERROR_VALIDATION_FAILED_EXT
		return VK_FALSE;
	}

	void setup_debugging(VkInstance instance)
	{
		vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
		vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

		VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{};
		debugUtilsMessengerCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugUtilsMessengerCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugUtilsMessengerCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugUtilsMessengerCI.pfnUserCallback = debug_message_callback;
		VkResult result = vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerCI, nullptr, &debug_utils_messenger);
		assert(result == VK_SUCCESS);
	}

	void free_debug_callback(VkInstance instance)
	{
		if (debug_utils_messenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(instance, debug_utils_messenger, nullptr);
		}
	}

}// namespace ocarina


