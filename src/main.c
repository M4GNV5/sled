// Main loader for sled. The entry point.
//
// Copyright (c) 2019, Adrian "vifino" Pistol <vifino@tty.sh>
// 
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// 
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "types.h"
#include "matrix.h"
#include "mod.h"
#include "timers.h"
#include "random.h"
#include "util.h"
#include "asl.h"
#include "oscore.h"
#include "taskpool.h"
#include "modloader.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>


static int outmodno = -1;

static oscore_mutex rmod_lock;
// Usually -1.
static int main_rmod_override = -1;
static asl_av_t main_rmod_override_args;

const char default_moduledir[] = DEFAULT_MODULEDIR;

#ifdef CIMODE
static int ci_iteration_count = 0;
#endif

static int deinit(void) {
	printf("Cleaning up GFX/BGM modules..."); fflush(stdout);
	int ret;
	modloader_deinitgfx();
	printf(" Done!\nCleaning up output module interface..."); fflush(stdout);
	if ((ret = matrix_deinit()) != 0)
		return ret;
	if ((ret = timers_deinit()) != 0)
		return ret;
	printf(" Done!\nCleaning up output module and filters..."); fflush(stdout);
	modloader_deinitend();
	printf(" Done!\nCleaning up bits and pieces..."); fflush(stdout);
	oscore_mutex_free(rmod_lock);
	if (main_rmod_override != -1)
		asl_clearav(&main_rmod_override_args);
	modloader_modpath = NULL;
	free(modloader_modpath);

	printf(" Done!\nCleaning up Taskpool..."); fflush(stdout);
	taskpool_forloop_free();
	taskpool_destroy(TP_GLOBAL);


	printf(" Done.\nGoodbye. :(\n");
	return 0;
}

/*
static int pick_next_random(int current_modno, ulong in) {
	oscore_mutex_lock(rmod_lock);
	if (main_rmod_override != -1) {
		int res = timer_add(in, main_rmod_override, main_rmod_override_args.argc, main_rmod_override_args.argv);
		main_rmod_override = -1;
		oscore_mutex_unlock(rmod_lock);
		return res;
	}
	oscore_mutex_unlock(rmod_lock);
	int next_mod;
	
	int usablemodcount = 0;
	mod_lock();
	for (int mod = 0; mod < MAX_MODULES; mod++) {
		if (mod_slot_has_mod(mod)) {
			if (strcmp(mod_get(mod)->type, "gfx") != 0)
				continue;
			usablemodcount++;
		}
	}
	if (usablemodcount > 1) {
		next_mod = -1;
		while (next_mod == -1) {
			int random = randn(modcount);
			next_mod = random;

			// Checks after.
			if (next_mod == current_modno) next_mod = -1;
			module* mod = mod_get(next_mod);
			if (!mod) {
				next_mod = -1;
			} else if (strcmp(mod->type, "gfx") != 0) {
				next_mod = -1;
			}
		}
	} else if (usablemodcount == 1) {
		next_mod = lastvalidmod;
	} else {
		in += 5000000;
		next_mod = -2;
	}
	mod_unlock();
	return timer_add(in, next_mod, 0, NULL);
}*/

static int pick_next_seq(int current_modno, ulong in) {
	oscore_mutex_lock(rmod_lock);
	if (main_rmod_override != -1) {
		int res = timer_add(in, main_rmod_override, main_rmod_override_args.argc, main_rmod_override_args.argv);
		main_rmod_override = -1;
		oscore_mutex_unlock(rmod_lock);
		return res;
	}
	oscore_mutex_unlock(rmod_lock);

	int next_mod = 0;

	// No modules, uhoh
	if (modloader_gfx_rotation.argc == 0) {
		in += 5000000;
		next_mod = -2;
	} else {
		next_mod = modloader_gfx_rotation.argv[0];
		// Did we find the current module in the rotation?
		int found_here = 0;
#ifdef CIMODE
		// Notably, this doesn't count if current_modno was invalid, so the CI iteration counter doesn't go off spuriously
		int hit_end_of_loop = current_modno >= 0;
#endif
		for (int i = 0; i < modloader_gfx_rotation.argc; i++) {
			if (found_here) {
				// The previous iteration was the current module, so this iteration is?
				next_mod = modloader_gfx_rotation.argv[i];
#ifdef CIMODE
				hit_end_of_loop = 0;
#endif
				break;
			}
			if (modloader_gfx_rotation.argv[i] == current_modno) {
				// Great!
				found_here = 1;
			}
		}
#ifdef CIMODE
		if (hit_end_of_loop) {
			ci_iteration_count++;
			if (ci_iteration_count > 10) { // maybe make this configurable, but its ok for now
				timers_quitting = 1;
				return 0;
			}
		}
#endif
	}
	return timer_add(in, next_mod, 0, NULL);
}

// this could also be easily rewritten to be an actual feature
static int pick_next(int current_modno, ulong in) {
//#ifdef CIMODE
	return pick_next_seq(current_modno, in);
//#else
//	return pick_next_random(current_modno, in);
//#endif
}

void main_force_random(int mnum, int argc, char ** argv) {
	asl_av_t bundled = {argc, argv};
	while (!timers_quitting) {
		oscore_mutex_lock(rmod_lock);
		if (main_rmod_override == -1) {
			main_rmod_override = mnum;
			main_rmod_override_args = bundled;
			oscore_mutex_unlock(rmod_lock);
			return;
		}
		oscore_mutex_unlock(rmod_lock);
		usleep(5000);
	}
	// Quits out without doing anything to prevent deadlock.
	asl_clearav(&bundled);
}

int usage(char* name) {
	printf("Usage: %s [-of]\n", name);
	printf("\t-m --modpath: Set directory that contains the modules to load.\n");
	printf("\t-o --output:  Set output module. Defaults to dummy.\n");
	printf("\t-f --filter:  Add a filter, can be used multiple times.\n");
	return 1;
}

static struct option longopts[] = {
	{ "modpath", required_argument, NULL, 'm' },
	{ "output",  required_argument, NULL, 'o' },
	{ "filter",  optional_argument, NULL, 'f' },
	{ NULL,      0,                 NULL, 0},
};

static int interrupt_count = 0;
static void interrupt_handler(int sig) {
	//
	if (interrupt_count == 0) {
		printf("sled: Quitting due to interrupt...\n");
		timers_doquit();
	} else if (interrupt_count == 1) {
		eprintf("sled: Warning: One more interrupt until ungraceful exit!\n");
	} else {
		eprintf("sled: Instantly panic-exiting. Bye.\n");
		exit(1);
	}

	interrupt_count++;
}

int sled_main(int argc, char** argv) {
	int ch;

	char outmod_c[256] = DEFAULT_OUTMOD;
	char* outarg = NULL;

	asl_av_t filternames = {0, NULL};
	asl_av_t filterargs = {0, NULL};

	while ((ch = getopt_long(argc, argv, "m:o:f:", longopts, NULL)) != -1) {
		switch(ch) {
		case 'm': {
			size_t len = strlen(optarg);
			char* str = calloc(len + 1, sizeof(char));
			util_strlcpy(str, optarg, len + 1);
			modloader_modpath = str;
			break;
		}
		case 'o': {
			size_t len = strlen(optarg);
			char* tmp = malloc((len + 1) * sizeof(char));
			assert(tmp);
			util_strlcpy(tmp, optarg, len + 1);
			char* arg = tmp;

			char* modname = strsep(&arg, ":");
			if (arg) {
				free(outarg);
				outarg = strdup(arg);
				assert(outarg);
			}
			util_strlcpy(outmod_c, modname, 256);
			free(tmp);
			break;
		}
		case 'f': {
			char* arg = strdup(optarg);

			char* modname = strsep(&arg, ":");
			char* fltarg = NULL;
			if (arg != NULL) {
				fltarg = strdup(arg);
				assert(fltarg);
			} else
				modname = optarg;
			char* str = strdup(modname);
			assert(str);
			asl_growav(&filternames, str);
			asl_growav(&filterargs, fltarg);
			break;
		}
		case '?':
		default:
			return usage(argv[0]);
		}
	}
	argc -= optind;
	argv += optind;

	int ret;

	// Initialize pseudo RNG.
	random_seed();

	// Prepare for module loading
	if (modloader_modpath == NULL) {
		modloader_modpath = strdup(default_moduledir);
		assert(modloader_modpath);
	}

	ret = modloader_initmod();
	if (ret) {
		eprintf("Failed to load the modloader tree. How'd this happen?\n");
		free(modloader_modpath);
		return ret;
	}

	// Load outmod
	char outmodname[4 + ARRAY_SIZE(outmod_c)];
	snprintf(outmodname, 4 + ARRAY_SIZE(outmod_c), "out_%s", outmod_c);
	char * outmodbuf2 = strdup(outmodname);
	assert(outmodbuf2);
	asl_pgrowav(&filternames, outmodbuf2);
	asl_pgrowav(&filterargs, outarg);
	outmodno = modloader_initout(&filternames, &filterargs);
	// No need for these anymore.
	asl_clearav(&filternames);
	asl_clearav(&filterargs);
	if (outmodno == -1) {
		eprintf("Failed to load the output/filter stack. This isn't good.\n");
		free(modloader_modpath);
		modloader_deinitend();
		return ret;
	}

	// Initialize Timers.
	ret = timers_init(outmodno);
	if (ret) {
		printf("Timers failed to initialize.\n");
		modloader_deinitend();
		return ret;
	}

	// Initialize Matrix.
	ret = matrix_init(outmodno);
	if (ret) {
		// Fail.
		printf("Matrix failed to initialize, which means someone's been making matrix_init more complicated. Uhoh.\n");
		timers_deinit();
		modloader_deinitend();
		return ret;
	}

	rmod_lock = oscore_mutex_new();

	ret = modloader_initgfx();
	if (ret)
		eprintf("Failed to load graphics modules (%i), continuing, what could possibly go wrong?\n", ret);

	// Initialize global task pool.
	int ncpus = oscore_ncpus();
	TP_GLOBAL = taskpool_create("taskpool", ncpus, ncpus*8);

	signal(SIGINT, interrupt_handler);

	// Startup.
	pick_next(-1, udate());

	int lastmod = -1;
	while (!timers_quitting) {
		timer tnext = timer_get();
		if (tnext.moduleno == -1) {
			// Queue random.
			pick_next(lastmod, udate() + TIME_SHORT * T_SECOND);
		} else {
			if (tnext.time > timers_wait_until(tnext.time)) {
				// Early break. Set this timer up for elimination by any 0-time timers that have come along
				if (tnext.time == 0)
					tnext.time = 1;
				timer_add(tnext.time, tnext.moduleno, tnext.args.argc, tnext.args.argv);
				continue;
			}
			if (tnext.moduleno >= 0) {
				assert(tnext.moduleno < mod_count());
				module* mod = mod_get(tnext.moduleno);
				if (!mod->is_valid_drawable) {
					printf("\n>> Undrawable module in view: %s ; skipping...\n", mod->name);
					asl_clearav(&tnext.args);
					continue;
				}
				if (tnext.moduleno != lastmod) {
					printf("\n>> Now drawing %s", mod->name);
					fflush(stdout);
					if (mod->reset) {
						mod->reset(tnext.moduleno);
					} else {
						printf("\n>> GFX module without reset shouldn't happen: %s\n", mod->name);
					}
				} else {
					printf(".");
					fflush(stdout);
				};
				ret = mod->draw(tnext.moduleno, tnext.args.argc, tnext.args.argv);
				asl_clearav(&tnext.args);
				lastmod = tnext.moduleno;
				if (ret != 0) {
					if (ret == 1) {
						if (lastmod != tnext.moduleno) // not an animation.
							printf("\nModule chose to pass its turn to draw.");
						pick_next(lastmod, udate() + T_MILLISECOND);
						lastmod = -1;
					} else {
						eprintf("Module %s failed to draw: Returned %i", mod->name, ret);
						timers_quitting = 1;
						deinit();
						return 7;
					}
				}
			} else {
				// Virtual null module
				printf(">> using virtual null module\n");
				asl_clearav(&tnext.args);
			}
		}
	}
	return deinit();
}
