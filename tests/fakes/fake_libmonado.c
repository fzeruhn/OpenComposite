/* A stand-in for Monado's libmonado (LibMonadoTests.cpp), scripted through environment variables:
 *   FAKE_MND_MAJOR    the major version it reports (default 1)
 *   FAKE_MND_BATTERY  "present,charging,charge" for every device (default "0,0,0")
 * Three devices: 0 "Fake HMD" (head), 1 "Fake Left" (left), 2 "Fake Right" (right); serials SN0, SN1, SN2. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define FAKE_API __declspec(dllexport)
#else
#define FAKE_API __attribute__((visibility("default")))
#endif

/* The process's environment: under /MT this library has a C runtime of its own, whose getenv doesn't see the tests'
 * _putenv_s, so on Windows it's read from the process itself */
static const char* fake_getenv(const char* name)
{
#ifdef _WIN32
	static char buffers[2][256];
	static int next;
	char* buffer = buffers[next++ % 2];
	DWORD n = GetEnvironmentVariableA(name, buffer, sizeof(buffers[0]));
	return n > 0 && n < sizeof(buffers[0]) ? buffer : NULL;
#else
	return getenv(name);
#endif
}

static const char* names[] = { "Fake HMD", "Fake Left", "Fake Right" };
static const char* serials[] = { "SN0", "SN1", "SN2" };
static const char* roles[] = { "head", "left", "right" };
static int root_storage;

FAKE_API int mnd_api_get_version(uint32_t* major, uint32_t* minor, uint32_t* patch)
{
	const char* m = fake_getenv("FAKE_MND_MAJOR");
	*major = m ? (uint32_t)atoi(m) : 1;
	*minor = 2;
	*patch = 0;
	return 0;
}

FAKE_API int mnd_root_create(void** out_root)
{
	*out_root = &root_storage;
	return 0;
}

FAKE_API void mnd_root_destroy(void** root)
{
	*root = NULL;
}

FAKE_API int mnd_root_get_device_count(void* root, uint32_t* out_count)
{
	(void)root;
	*out_count = 3;
	return 0;
}

FAKE_API int mnd_root_get_device_info_string(void* root, uint32_t index, int prop, const char** out)
{
	(void)root;
	if (index > 2)
		return -1;
	if (prop == 0)
		*out = names[index];
	else if (prop == 1)
		*out = serials[index];
	else
		return -1;
	return 0;
}

FAKE_API int mnd_root_get_device_from_role(void* root, const char* role, int32_t* out_index)
{
	(void)root;
	for (int i = 0; i < 3; i++) {
		if (strcmp(role, roles[i]) == 0) {
			*out_index = i;
			return 0;
		}
	}
	*out_index = -1;
	return -1;
}

FAKE_API int mnd_root_get_device_battery_status(void* root, uint32_t index, bool* present, bool* charging, float* charge)
{
	(void)root;
	if (index > 2)
		return -1;
	int p = 0, c = 0;
	float level = 0;
	const char* b = fake_getenv("FAKE_MND_BATTERY");
	if (b)
		sscanf(b, "%d,%d,%f", &p, &c, &level);
	*present = p != 0;
	*charging = c != 0;
	*charge = level;
	return 0;
}
