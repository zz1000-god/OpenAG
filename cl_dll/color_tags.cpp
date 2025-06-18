#include "color_tags.h"
#include "hud.h"
#include "cl_util.h"
extern cvar_t *cl_colortext;

namespace color_tags {
	namespace {
		// R, G, B.
		constexpr int colors[][3] = {
			{ 255, 0,   0   },
			{ 0,   255, 0   },
			{ 255, 255, 0   },
			{ 0,   0,   255 },
			{ 0,   255, 255 },
			{ 255, 0,   255 },
			{ 136, 136, 136 },
			{ 255, 255, 255 }
		};
	}

	bool are_color_tags_enabled() {
		return cl_colortext && cl_colortext->value != 0;
	}

	void strip_color_tags(char* dest, const char* src, size_t count) {
		if (count == 0)
			return;

		for (; *src != '\0' && count > 1; ++src) {
			if (are_color_tags_enabled() && src[0] == '^' && src[1] >= '0' && src[1] <= '9') {
				++src; // Пропускаємо color tag
				continue;
			} else {
				*dest++ = *src;
				--count;
			}
		}
		*dest = '\0';
	}

	char* strip_color_tags_thread_unsafe(const char* string) {
		static char buf[2048];
		strip_color_tags(buf, string, ARRAYSIZE(buf));
		return buf;
	}

	bool contains_color_tags(const char* string) {
		if (!are_color_tags_enabled())
			return false;

		for (; *string != '\0'; ++string) {
			if (string[0] == '^' && string[1] >= '0' && string[1] <= '9')
				return true;
		}
		return false;
	}

	void for_each_colored_substr(char* string,
	                             std::function<void(const char* string,
	                                                bool custom_color,
	                                                int r,
	                                                int g,
	                                                int b)> function) {
		// Якщо color tags вимкнені, просто передаємо весь текст без обробки
		if (!are_color_tags_enabled()) {
			function(string, false, 255, 160, 0); // Half-Life стандартний жовтий колір
			return;
		}

		bool custom_color = false;
		int r = 255, g = 160, b = 0; // Half-Life стандартний жовтий колір за замовчуванням
		char *temp = string;
		
		while ((temp = strchr(temp, '^'))) {
			char color_index = temp[1];
			if (color_index >= '0' && color_index <= '9') {
				if (temp != string) {
					*temp = '\0';
					function(string, custom_color, r, g, b);
					*temp = '^';
				}
				string = temp + 2;
				temp = temp + 2;
				
				if (color_index == '0' || color_index == '9') {
					custom_color = false;
					r = 255; g = 160; b = 0; // Half-Life стандартний жовтий
				} else {
					custom_color = true;
					r = colors[color_index - '1'][0];
					g = colors[color_index - '1'][1];
					b = colors[color_index - '1'][2];
				}
			} else {
				++temp;
			}
		}
		
		if (string[0] != '\0')
			function(string, custom_color, r, g, b);
	}
}
