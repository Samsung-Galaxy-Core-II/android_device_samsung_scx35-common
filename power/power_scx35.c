/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2014 The CyanogenMod Project
 * Copyright (C) 2017 The Lineage Project
 * Copyright (C) 2014-2015 Andreas Schneider <asn@cryptomilk.org>
 * Copyright (C) 2014-2015 Christopher N. Hesse <raymanfx@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#define LOG_TAG "SCX35PowerHAL"
/* #define LOG_NDEBUG 0 */
#include <log/log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define PARAM_MAXLEN      10

#define CPU_SYSFS_PATH          "/sys/devices/system/cpu"
#define CPUFREQ_SYSFS_PATH      CPU_SYSFS_PATH "/cpufreq/"
#define CPU_MAX_FREQ_PATH       CPU_SYSFS_PATH "/cpu0/cpufreq/cpuinfo_max_freq"
#define SCALING_GOVERNOR_PATH   CPU_SYSFS_PATH "/cpu0/cpufreq/scaling_governor"
#define SCALING_MAX_FREQ_PATH   CPU_SYSFS_PATH "/cpu0/cpufreq/scaling_max_freq"
#define SCALING_MIN_FREQ_PATH   CPU_SYSFS_PATH "/cpu0/cpufreq/scaling_min_freq"
#define PANEL_BRIGHTNESS        "/sys/class/backlight/panel/brightness"

/* Interactive governor */
#define HISPEED_FREQ_PATH       "/hispeed_freq"
#define IO_IS_BUSY_PATH         "/io_is_busy"
#define BOOSTPULSE_PATH         "/boostpulse"

struct touch_path {
	char* touchscreen_power_path;
	char* touchkey_power_path;
	bool touchkey_blocked;
};

static char CPU_INTERACTIVE_PATH[80];

enum power_profile_e {
	PROFILE_POWER_SAVE = 0,
	PROFILE_BALANCED,
	PROFILE_HIGH_PERFORMANCE,
	PROFILE_MAX
};
static enum power_profile_e current_power_profile = PROFILE_HIGH_PERFORMANCE;

/**********************************************************
 *** HELPER FUNCTIONS
 **********************************************************/

static int sysfs_read(char *path, char *s, int num_bytes)
{
	char errno_str[64];
	int len;
	int ret = 0;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		strerror_r(errno, errno_str, sizeof(errno_str));
		ALOGE("Error opening %s: %s\n", path, errno_str);
		return -1;
	}

	len = read(fd, s, num_bytes - 1);
	if (len < 0) {
		strerror_r(errno, errno_str, sizeof(errno_str));
		ALOGE("Error reading from %s: %s\n", path, errno_str);

		ret = -1;
	} else {
		s[len] = '\0';
	}

	close(fd);

	return ret;
}

static void sysfs_write(const char *path, char *s) {

	char errno_str[64];
	int len;
	int fd;

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		strerror_r(errno, errno_str, sizeof(errno_str));
		ALOGE("Error opening %s: %s\n", path, errno_str);
		return;
	}

	len = write(fd, s, strlen(s));

	if (len < 0) {
		strerror_r(errno, errno_str, sizeof(errno_str));
		ALOGE("Error writing to %s: %s\n", path, errno_str);
	}

	close(fd);
}

static void get_cpu_interactive_paths() {

	char governor[20];

	if (sysfs_read(SCALING_GOVERNOR_PATH, governor,
		sizeof(governor)) == -1) {
		return;
	} else {

        // Strip newline at the end.
        int len = strlen(governor);

        len--;

        while (len >= 0 && (governor[len] == '\n' || governor[len] == '\r'))
            governor[len--] = '\0';
	}

	if (strncmp(governor, "interactive", 11) == 0 ||
		strncmp(governor, "intelliactive", 13) == 0) {
		sprintf(CPU_INTERACTIVE_PATH, "%s%s", CPUFREQ_SYSFS_PATH, governor);
		ALOGI("Current interactive governor is: %s", governor);
	}
}

static void cpu_interactive_read(const char *param, char s[PARAM_MAXLEN])
{
    char path[PATH_MAX];

	sprintf(path, "%s%s", CPU_INTERACTIVE_PATH, param);
	sysfs_read(path, s, PARAM_MAXLEN);
}

static void cpu_interactive_write(const char *param, char s[PARAM_MAXLEN])
{
    char path[PATH_MAX];

    sprintf(path, "%s%s", CPU_INTERACTIVE_PATH, param);
    sysfs_write(path, s);
}

static unsigned int read_panel_brightness() {

	unsigned int i, ret = 0;
	int read_status;
	// brightness can range from 0 to 255, so max. 3 chars + '\0'
	char panel_brightness[4];

	read_status = sysfs_read(PANEL_BRIGHTNESS, panel_brightness, sizeof(PANEL_BRIGHTNESS));
	if (read_status < 0) {
		ALOGE("%s: Failed to read panel brightness from %s!\n", __func__, PANEL_BRIGHTNESS);
		return -1;
	}

	for (i = 0; i < (sizeof(panel_brightness) / sizeof(panel_brightness[0])); i++) {
		if (isdigit(panel_brightness[i])) {
			ret += (panel_brightness[i] - '0');
		}
	}

	ALOGV("%s: Panel brightness is: %d", __func__, ret);

	return ret;
}

static void send_boostpulse(int boostpulse_fd)
{
	int len;

    if (boostpulse_fd < 0) {
        return;
    }

    len = write(boostpulse_fd, "1", 1);
    if (len < 0) {
        ALOGE("Error writing to boostpulse path: %s", strerror(errno));
    }
}

/**********************************************************
 *** POWER FUNCTIONS
 **********************************************************/

static void set_power_profile(enum power_profile_e profile)
{

	char cpu_hispeed_freq[10];
	char cpu_min_freq[10];
	char cpu_max_freq[10];

	if (current_power_profile == profile) {
		return;
	}

	ALOGV("%s: profile=%d", __func__, profile);

	switch (profile) {
	case PROFILE_POWER_SAVE:
		// Limit to min freq which was set through sysfs
		sysfs_read(SCALING_MIN_FREQ_PATH, cpu_min_freq,
				   sizeof(cpu_min_freq));
		sysfs_write(SCALING_MAX_FREQ_PATH, cpu_min_freq);
		ALOGD("%s: set powersave mode", __func__);
		break;
	case PROFILE_BALANCED:
		// Limit to hispeed freq
		cpu_interactive_read(HISPEED_FREQ_PATH, cpu_hispeed_freq);
		sysfs_write(SCALING_MAX_FREQ_PATH, cpu_hispeed_freq);
		ALOGD("%s: set balanced mode", __func__);
		break;
	case PROFILE_HIGH_PERFORMANCE:
		// Restore normal max freq
		sysfs_read(CPU_MAX_FREQ_PATH, cpu_max_freq,
				   sizeof(cpu_max_freq));
		sysfs_write(SCALING_MAX_FREQ_PATH, cpu_max_freq);
		ALOGD("%s: set performance mode", __func__);
		break;
	case PROFILE_MAX:
		break;
	}

	current_power_profile = profile;
}

static void find_input_nodes(struct touch_path *touch) {

	const char filename[] = "name";
	char errno_str[64];
	struct dirent *de;
	char file_content[20];
	char *path = NULL;
	char *node_path = NULL;
	char dir[32];
	size_t pathsize;
	size_t node_pathsize;
	DIR *d;
	uint32_t i;

	for (i = 0; i < 20; i++) {
		snprintf(dir, sizeof(dir), "/sys/class/input/input%d", i);
		d = opendir(dir);
		if (d == NULL)
			return;

		while ((de = readdir(d)) != NULL) {
			/*
			 * I don't think it would be possible to get
			 * a null d_name or a null-byte at d_name[0]
			 *
			 * Skip all hidden files and directories, and
			 * current and parent directory.
			 */
			if (de->d_name[0] == '.')
				continue;

			if (strncmp(filename, de->d_name, sizeof(filename)) == 0) {
				pathsize = strlen(dir) + strlen(de->d_name) + 2;
				node_pathsize = strlen(dir) + strlen("enabled") + 2;

				path = malloc(pathsize);
				node_path = malloc(node_pathsize);
				if (path == NULL || node_path == NULL) {
					if (path)
						free(path);
					if(node_path)
						free(node_path);
					strerror_r(errno, errno_str, sizeof(errno_str));
					ALOGE("Out of memory: %s\n", errno_str);
					return;
				}

				snprintf(path, pathsize, "%s/%s", dir, filename);
				sysfs_read(path, file_content, sizeof(file_content));
				// path will be left unused onwards so free it
				free(path);

				snprintf(node_path, node_pathsize, "%s/%s", dir, "enabled");

				if (strncmp(file_content, "sec_touchkey", 12) == 0) {
					ALOGV("%s: found touchkey path: %s\n", __func__, node_path);
					touch->touchkey_power_path = node_path;
				} else if (strncmp(file_content, "sec_touchscreen", 15) == 0) {
					ALOGV("%s: found touchscreen path: %s\n", __func__, node_path);
					touch->touchscreen_power_path = node_path;
				}
			}
		}
		closedir(d);
	}
}

/**********************************************************
 *** INIT FUNCTIONS
 **********************************************************/
/*
 * The init function performs power management setup actions at runtime
 * startup, such as to set default cpufreq parameters.  This is called only by
 * the Power HAL instance loaded by PowerManagerService.
 */
void power_init() {
	get_cpu_interactive_paths();
}

/*
 * The setInteractive function performs power management actions upon the
 * system entering interactive state (that is, the system is awake and ready
 * for interaction, often with UI devices such as display and touchscreen
 * enabled) or non-interactive state (the system appears asleep, display
 * usually turned off).  The non-interactive state is usually entered after a
 * period of inactivity, in order to conserve battery power during such
 * inactive periods.
 *
 * Typical actions are to turn on or off devices and adjust cpufreq parameters.
 * This function may also call the appropriate interfaces to allow the kernel
 * to suspend the system to low-power sleep state when entering non-interactive
 * state, and to disallow low-power suspend when the system is in interactive
 * state.  When low-power suspend state is allowed, the kernel may suspend the
 * system whenever no wakelocks are held.
 *
 * on is non-zero when the system is transitioning to an interactive / awake
 * state, and zero when transitioning to a non-interactive / asleep state.
 *
 * This function is called to enter non-interactive state after turning off the
 * screen (if present), and called to enter interactive state prior to turning
 * on the screen.
 */
void power_set_interactive(int on) {

	struct touch_path *touch = (struct touch_path *)calloc(1, sizeof(struct touch_path));
	struct stat sb;
	char touchkey_node[2];
	int rc;

	ALOGV("power_set_interactive: %d\n", on);

	// Do not disable any input devices if the screen is on but we are in a non-interactive state
	if (!on) {
		if (read_panel_brightness() > 0) {
			ALOGV("%s: Moving to non-interactive state, but screen is still on,"
			      " not disabling input devices\n", __func__);
		goto out;
		}
	}
	if (!touch) {
		ALOGE("%s: failed to allocate memory", __func__);
		goto out;
	}
	find_input_nodes(touch);

	if (touch->touchscreen_power_path)
		sysfs_write(touch->touchscreen_power_path, on ? "1" : "0");

	rc = stat(touch->touchkey_power_path, &sb);
	if (rc < 0) {
		goto out;
	}

	if (!on) {
		if (sysfs_read(touch->touchkey_power_path, touchkey_node,
			       sizeof(touchkey_node)) == 0) {
			/*
			* If touchkey_node is 0, the keys have been disabled by another component
			* (for example cmhw), which means we don't want them to be enabled when resuming
			* from suspend.
			*/
			if ((touchkey_node[0] - '0') == 0) {
				touch->touchkey_blocked = true;
			} else {
				touch->touchkey_blocked = false;
				sysfs_write(touch->touchkey_power_path, "0");
			}
		}
	} else {
		if (!touch->touchkey_blocked) {
			sysfs_write(touch->touchkey_power_path, "1");
		}
	}

out:
	if (touch) {
		if (touch->touchkey_power_path)
			free(touch->touchkey_power_path);
		if (touch->touchscreen_power_path)
			free(touch->touchscreen_power_path);
		free(touch);
	}
	cpu_interactive_write(IO_IS_BUSY_PATH, on ? "1" : "0");
	ALOGV("power_set_interactive: %d done\n", on);
}

/*
 * The powerHint function is called to pass hints on power requirements, which
 * may result in adjustment of power/performance parameters of the cpufreq
 * governor and other controls.
 *
 * The possible hints are:
 *
 * POWER_HINT_VSYNC
 *
 *     Foreground app has started or stopped requesting a VSYNC pulse
 *     from SurfaceFlinger.  If the app has started requesting VSYNC
 *     then CPU and GPU load is expected soon, and it may be appropriate
 *     to raise speeds of CPU, memory bus, etc.  The data parameter is
 *     non-zero to indicate VSYNC pulse is now requested, or zero for
 *     VSYNC pulse no longer requested.
 *
 * POWER_HINT_INTERACTION
 *
 *     User is interacting with the device, for example, touchscreen
 *     events are incoming.  CPU and GPU load may be expected soon,
 *     and it may be appropriate to raise speeds of CPU, memory bus,
 *     etc.  The data parameter is unused.
 *
 * POWER_HINT_LOW_POWER
 *
 *     Low power mode is activated or deactivated. Low power mode
 *     is intended to save battery at the cost of performance. The data
 *     parameter is non-zero when low power mode is activated, and zero
 *     when deactivated.
 *
 * POWER_HINT_CPU_BOOST
 *
 *     An operation is happening where it would be ideal for the CPU to
 *     be boosted for a specific duration. The data parameter is an
 *     integer value of the boost duration in microseconds.
 */
void power_hint(power_hint_t hint, void *data)
{
	switch (hint) {
		case POWER_HINT_INTERACTION: {
			if (current_power_profile == PROFILE_POWER_SAVE) {
				return;
			}
			char boostpulse_path[PATH_MAX];
			int boostpulse_fd;

			sprintf(boostpulse_path, "%s%s", CPU_INTERACTIVE_PATH, BOOSTPULSE_PATH);
			boostpulse_fd = open(boostpulse_path, O_WRONLY);

			if (boostpulse_fd < 0) {
				ALOGE("Error opening %s: %s\n", boostpulse_path, strerror(errno));
			}

			ALOGV("%s: POWER_HINT_INTERACTION", __func__);
			send_boostpulse(boostpulse_fd);
			close(boostpulse_fd);
			break;
		}
		case POWER_HINT_VSYNC: {
			ALOGV("%s: POWER_HINT_VSYNC", __func__);
			break;
		}
		case POWER_HINT_SET_PROFILE: {
			int profile = *((intptr_t *)data);

			ALOGV("%s: POWER_HINT_SET_PROFILE", __func__);
	
			set_power_profile(profile);
			break;
		}
		default:
			break;
	}
}

int get_number_of_profiles()
{
    return PROFILE_MAX;
}
