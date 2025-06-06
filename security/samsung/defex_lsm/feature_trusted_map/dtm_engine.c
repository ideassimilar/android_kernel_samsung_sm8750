// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2022 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kobject.h> // struct kset is required by include/defex_debug.h

#include "include/defex_debug.h"
#include "include/defex_rules.h"
#include "include/dtm.h"
#include "include/dtm_engine.h"
#include "include/dtm_log.h"
#include "include/dtm_utils.h"
#include "include/ptree.h"

#define DTM_ANY_VALUE "*" /* wildcard value for files and arguments */
#if defined(DEFEX_TM_DEFAULT_POLICY_ENABLE) || defined(DEFEX_GKI)
/* Kernel-only builds don't use DEFEX's dynamic policy loading mechanism. */
#define USE_EMBEDDED_POLICY
static struct PPTree embedded_header;
/* File with hardcoded policy */
#include "dtm_engine_defaultpolicy.h"
#endif

#ifdef DEFEX_KUNIT_ENABLED
static struct PPTree override_header, *pptree_override;

void dtm_engine_override_data(const unsigned char *p)
{
	if (p) {
		defex_log_warn("DTM: data overridden");
		pptree_set_data(pptree_override = &override_header, p);
	} else {
		defex_log_warn("DTM: override data back to normal");
		pptree_override = 0;
	}
}
#endif

static int dtm_check_stdin(struct dtm_context *context, int allowed_stdin_modes)
{
	if (unlikely(!(dtm_get_stdin_mode_bit(context) & allowed_stdin_modes))) {
		dtm_report_violation(DTM_STDIN_VIOLATION, context);
		return DTM_DENY;
	}
	return DTM_ALLOW;
}

/*
 * Enforces DTM policy for an exec system call.
 */
int dtm_enforce(struct dtm_context *context)
{
	const char *callee_path, *caller_path;
	const char *program_name;
	int ret, argc, call_argc;
	const char *argument_value;
	struct PPTree *pptree; /* effective header of policy rules */
	struct PPTreeContext pp_ctx; /* context for policy search */
	static char first_run = 1; /* flag for one-time policy actions */

	if (first_run) {
		// Minimum log: make obvious DTM is active
		if (dtm_tree.data)
			defex_log_warn("DTM: policy found, "
#ifdef DEFEX_PERMISSIVE_TM
					"permissive"
#else
					"enforcing"
#endif
				" mode");
		else
			defex_log_warn("DTM: dynamic policy not loaded");
		first_run = 0;
#ifdef USE_EMBEDDED_POLICY
		pptree_set_data(&embedded_header, dtm_engine_defaultpolicy);
#endif
	}

	dtm_trace("Pid : %d %d", current->tgid, current->pid);

	if (!context || unlikely(!is_dtm_context_valid(context))) {
		dtm_trace("(0) TMED no or invalid context");
		return DTM_DENY;
	}
	/* Check callee */
	callee_path = dtm_get_callee_path(context);
	if (unlikely(!callee_path)) {
		dtm_trace("(1) TMED null callee");
		return DTM_DENY;
	}

#ifdef DEFEX_KUNIT_ENABLED
	if (pptree_override) /* test code has opportunity to use test policy instead */
		pptree = pptree_override;
	else
#endif
#ifdef USE_EMBEDDED_POLICY /* try dynamic policy first, use embedded if not found */
		pptree = dtm_tree.data ? &dtm_tree : &embedded_header;
#else /* only dynamically loaded policy is acceptable */
		pptree = &dtm_tree;
#endif
	if (!pptree->data) { /* Should never happen */
		defex_log_warn("(0) TME: neither dynamic nor hardcoded rules loaded");
		return DTM_ALLOW;
	}
	memset(&pp_ctx, 0, sizeof(pp_ctx));
	if (!(pptree_find_path(pptree,
			       *callee_path == '/' ?
			       callee_path + 1 : callee_path, '/', &pp_ctx) &&
	      (pp_ctx.types & PTREE_DATA_PATH))) {
		dtm_trace("(2) TME callee '%s' not found", callee_path);
		return DTM_ALLOW;
	}
	/* Check caller */
	caller_path = dtm_get_caller_path(context);
	if (unlikely(!caller_path)) {
		dtm_trace("(3) TMED callee '%s': null caller", callee_path);
		return DTM_DENY;
	}
	if (!(pptree_find_path(pptree, caller_path + 1, '/', &pp_ctx) &&
	      (pp_ctx.types & PTREE_DATA_PATH))) {
		dtm_trace("(4) TMED callee '%s': caller '%s' not found",
			callee_path, caller_path);
		dtm_report_violation(DTM_CALLER_VIOLATION, context);
		return DTM_DENY;
	}
	/* Check program, if any */
	program_name = dtm_get_program_name(context);
	if (!program_name) {
		dtm_trace("(5) TMED callee '%s', caller '%s': null program",
			callee_path, caller_path);
		return DTM_DENY;
	}
	pp_ctx.types |= PTREE_FIND_PEEK;

	// Try exact match first
	/* Originally, the 2nd and 3rd args to pptree_find_path below were
	 * program_name == '/' ? program_name + 1 : program_nameand /'
	 * respectively. However, to keep compatibility with policy entries
	 * which record only a program's basename, we strip the directory part
	 * at build time, and correspondingly here test only the basename.
	 */
	if (pptree_find_path(pptree, program_name, 0, &pp_ctx) &&
		      (pp_ctx.types & PTREE_DATA_INT2)) {
		pp_ctx.types |= PTREE_FIND_PEEKED;
		pptree_find_path(pptree, "", 0, &pp_ctx);
		pp_ctx.types &= ~PTREE_FIND_PEEKED;
	} else {
		// now try '*'
		pp_ctx.types &= ~PTREE_FIND_PEEK;
		if (pptree_find_path(pptree, DTM_ANY_VALUE, 0, &pp_ctx)) {
			dtm_trace("[TME callee '%s', caller '%s': program may be '*...'",
			       callee_path, caller_path);
		} else {
			dtm_trace("(6) TMED callee '%s', caller '%s': program '%s' not found",
				callee_path, caller_path, program_name);
			dtm_report_violation(DTM_PROGRAM_VIOLATION, context);
			return DTM_DENY;
		}
	}
	/* Check standard input mode */
	if (pp_ctx.types & PTREE_DATA_INT2) {
		ret = dtm_check_stdin(context, pp_ctx.value.int2);
		if (unlikely(ret != DTM_ALLOW)) {
			dtm_trace("(7) TMED callee '%s', caller '%s', program '%s':"
				" stdin mode %d, should be %d",
				callee_path, caller_path,
				program_name ? program_name : "(null)",
				dtm_get_stdin_mode_bit(context), pp_ctx.value.int2);
			return ret;
		}
	}
	/* Check program arguments, if any */
	pp_ctx.types |= PTREE_FIND_CONTINUE;
	pp_ctx.types &= ~PTREE_FIND_PEEKED;
	for (call_argc = context->callee_argc, argc = 1;
	     argc < call_argc && pptree_child_count(pptree, &pp_ctx);
	     ++argc) {
		pp_ctx.types |= PTREE_FIND_PEEK;
		argument_value = dtm_get_callee_arg(context, argc);
		if (pptree_find_path(pptree, argument_value, 0, &pp_ctx)) {
			pp_ctx.types |= PTREE_FIND_PEEKED;
			pptree_find_path(pptree, "", 0, &pp_ctx);
			pp_ctx.types &= ~PTREE_FIND_PEEKED;
		} else {
			pp_ctx.types &= ~PTREE_FIND_PEEK;
			if (pptree_find_path(pptree, DTM_ANY_VALUE, 0, &pp_ctx)) {
				dtm_trace("(8) TME callee '%s', caller '%s', program '%s':"
					" any arguments accepted",
					callee_path, caller_path, program_name);
				return DTM_ALLOW;
			}
			dtm_trace(
				"(9) TMED callee '%s', caller '%s', program '%s':"
				" argument '%s' (%d of %d) not found",
				callee_path, caller_path, program_name,
				argument_value ? argument_value : "(null)",
				argc, call_argc);
			dtm_report_violation(DTM_ARGUMENTS_VIOLATION, context);
			return DTM_DENY;
		}
#ifdef __NEVER_DEFINED__
		if (pp_ctx.value.bits) {
			dtm_trace("(10) TME callee '%s', caller '%s', program '%s':"
				" argument '%s' accepts '*'",
				callee_path, caller_path, program_name,
				argument_value ? argument_value : "(null)");
			return DTM_ALLOW;
		}
#endif
	}
	if (call_argc > 1 && pptree_get_offset(pptree, &pp_ctx) &&
		!pptree_find_path(pptree, DTM_ANY_VALUE, 0, &pp_ctx)) {
		dtm_trace(
			"(11) TMED callee '%s', caller '%s', program '%s': %d argument(s),"
				" more required",
			callee_path, caller_path, program_name,
			call_argc);
		dtm_report_violation(DTM_ARGUMENTS_VIOLATION, context);
		return DTM_DENY;
	}
#if DTM_TRACE
	if (call_argc && argc == call_argc)
		dtm_trace("(12) TME callee '%s', caller '%s', program '%s':"
			" all %d argument(s) checked",
			callee_path, caller_path, program_name, call_argc);
#endif
	return DTM_ALLOW;
}
