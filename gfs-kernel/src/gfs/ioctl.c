/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>
#include <linux/gfs_ioctl.h>

#include "gfs.h"
#include "bmap.h"
#include "dio.h"
#include "dir.h"
#include "eattr.h"
#include "file.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "ioctl.h"
#include "log.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"

typedef int (*gi_filler_t) (struct gfs_inode *ip,
			    struct gfs_ioctl *gi,
			    char *buf,
			    unsigned int size,
			    unsigned int *count);

#define ARG_SIZE (32)

/**
 * gi_skeleton - Setup a buffer that functions can print into
 * @ip:
 * @gi:
 * @filler:
 *
 * Returns: -errno or count of bytes copied to userspace
 */

static int
gi_skeleton(struct gfs_inode *ip, struct gfs_ioctl *gi,
	    gi_filler_t filler)
{
	ENTER(GFN_GI_SKELETON)
	unsigned int size = gfs_tune_get(ip->i_sbd, gt_lockdump_size);
        char *buf;
	unsigned int count = 0;
        int error;

	if (size > gi->gi_size)
		size = gi->gi_size;

        buf = kmalloc(size, GFP_KERNEL);
        if (!buf)
                RETURN(GFN_GI_SKELETON, -ENOMEM);

        error = filler(ip, gi, buf, size, &count);
	if (error)
		goto out;

	if (copy_to_user(gi->gi_data, buf, count + 1))
		error = -EFAULT;
	else
		error = count + 1;

 out:
	kfree(buf);

	RETURN(GFN_GI_SKELETON, error);
}

/**
 * gi_get_cookie - Return the "cookie" (identifying string) for a
 *                 filesystem mount
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_get_cookie(struct gfs_inode *ip,
	      struct gfs_ioctl *gi,
	      char *buf,
	      unsigned int size,
	      unsigned int *count)
{
	ENTER(GFN_GI_GET_COOKIE)
        int error = -ENOBUFS;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_COOKIE, -EINVAL);

	gfs_printf("version 0\n");
        gfs_printf("%lu", (unsigned long)ip->i_sbd);

        error = 0;

 out:
        RETURN(GFN_GI_GET_COOKIE, error);
}

/**
 * gi_get_super - Return the "struct gfs_sb" for a filesystem
 * @sdp:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_get_super(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_GET_SUPER)
	struct gfs_holder sb_gh;
	struct buffer_head *bh;
	struct gfs_sb *sb;
	int error;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_SUPER, -EINVAL);
	if (gi->gi_size != sizeof(struct gfs_sb))
		RETURN(GFN_GI_GET_SUPER, -EINVAL);

	sb = kmalloc(sizeof(struct gfs_sb), GFP_KERNEL);
	if (!sb)
		RETURN(GFN_GI_GET_SUPER, -ENOMEM);

	error = gfs_glock_nq_num(sdp,
				 GFS_SB_LOCK, &gfs_meta_glops,
				 LM_ST_SHARED, 0, &sb_gh);
	if (error)
		goto out;

	error = gfs_dread(sb_gh.gh_gl, GFS_SB_ADDR >> sdp->sd_fsb2bb_shift,
			  DIO_START | DIO_WAIT, &bh);
	if (error) {
		gfs_glock_dq_uninit(&sb_gh);
		goto out;
	}
	gfs_sb_in(sb, bh->b_data);
	brelse(bh);

	gfs_glock_dq_uninit(&sb_gh);

	if (copy_to_user(gi->gi_data, sb,
			 sizeof(struct gfs_sb)))
	        error = -EFAULT;
	else
		error = sizeof(struct gfs_sb);

 out:
	kfree(sb);

	RETURN(GFN_GI_GET_SUPER, error);
}

/**
 * gi_get_args - Return the mount arguments
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_get_args(struct gfs_inode *ip,
	    struct gfs_ioctl *gi,
	    char *buf,
	    unsigned int size,
	    unsigned int *count)
{
	ENTER(GFN_GI_GET_ARGS)
	struct gfs_args *args = &ip->i_sbd->sd_args;
	int error = -ENOBUFS;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_ARGS, -EINVAL);

	gfs_printf("version 0\n");
	gfs_printf("lockproto %s\n", args->ar_lockproto);
	gfs_printf("locktable %s\n", args->ar_locktable);
	gfs_printf("hostdata %s\n", args->ar_hostdata);
	gfs_printf("ignore_local_fs %d\n", args->ar_ignore_local_fs);
	gfs_printf("localcaching %d\n", args->ar_localcaching);
        gfs_printf("localflocks %d\n", args->ar_localflocks);
        gfs_printf("oopses_ok %d\n", args->ar_oopses_ok);
        gfs_printf("upgrade %d\n", args->ar_upgrade);
	gfs_printf("num_glockd %u\n", args->ar_num_glockd);
        gfs_printf("posix_acls %d\n", args->ar_posix_acls);
        gfs_printf("suiddir %d\n", args->ar_suiddir);

	error = 0;
	
 out:
	RETURN(GFN_GI_GET_ARGS, error);
}

/**
 * gi_get_lockstruct - Return the information in the FS' lockstruct
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_get_lockstruct(struct gfs_inode *ip,
		  struct gfs_ioctl *gi,
		  char *buf,
		  unsigned int size,
		  unsigned int *count)
{
	ENTER(GFN_GI_GET_LOCKSTRUCT)
	struct lm_lockstruct *ls = &ip->i_sbd->sd_lockstruct;
        int error = -ENOBUFS;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_LOCKSTRUCT, -EINVAL);

	gfs_printf("version 0\n");
        gfs_printf("jid %u\n", ls->ls_jid);
        gfs_printf("first %u\n", ls->ls_first);
        gfs_printf("lvb_size %u\n", ls->ls_lvb_size);
        gfs_printf("flags %d\n", ls->ls_flags);

	error = 0;

 out:
        RETURN(GFN_GI_GET_LOCKSTRUCT, error);
}

/**
 * gi_get_stat_gfs - Return a filesystem's space usage information
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_get_stat_gfs(struct gfs_inode *ip,
		struct gfs_ioctl *gi,
		char *buf,
		unsigned int size,
		unsigned int *count)
{
	ENTER(GFN_GI_GET_STAT_GFS)
	struct gfs_stat_gfs sg;
        int error;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_STAT_GFS, -EINVAL);

	error = gfs_stat_gfs(ip->i_sbd, &sg, TRUE);
	if (error)
		RETURN(GFN_GI_GET_STAT_GFS, error);

	error = -ENOBUFS;

	gfs_printf("version 0\n");
	gfs_printf("bsize %u\n", ip->i_sbd->sd_sb.sb_bsize);
        gfs_printf("total_blocks %"PRIu64"\n", sg.sg_total_blocks);
        gfs_printf("free %"PRIu64"\n", sg.sg_free);
        gfs_printf("used_dinode %"PRIu64"\n", sg.sg_used_dinode);
        gfs_printf("free_dinode %"PRIu64"\n", sg.sg_free_dinode);
        gfs_printf("used_meta %"PRIu64"\n", sg.sg_used_meta);
        gfs_printf("free_meta %"PRIu64"\n", sg.sg_free_meta);

	error = 0;

 out:
        RETURN(GFN_GI_GET_STAT_GFS, error);
}

/**
 * handle_roll - Read a atomic_t as an unsigned int
 * @a: a counter
 *
 * if @a is negative, reset it to zero
 *
 * Returns: the value of the counter
 */

static unsigned int
handle_roll(atomic_t *a)
{
	ENTER(GFN_HANDLE_ROLL)
	int x = atomic_read(a);
	if (x < 0) {
		atomic_set(a, 0);
		RETURN(GFN_HANDLE_ROLL, 0);
	}
	RETURN(GFN_HANDLE_ROLL, (unsigned int)x);
}

/**
 * gi_get_counters - Return usage counters
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_get_counters(struct gfs_inode *ip,
		struct gfs_ioctl *gi,
		char *buf,
		unsigned int size,
		unsigned int *count)
{
	ENTER(GFN_GI_GET_COUNTERS)
	struct gfs_sbd *sdp = ip->i_sbd;
        int error = -ENOBUFS;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_COUNTERS, -EINVAL);

	gfs_printf("version 0\n");
	gfs_printf("sd_glock_count:locks::%d\n",
		   atomic_read(&sdp->sd_glock_count));
	gfs_printf("sd_glock_held_count:locks held::%d\n",
		   atomic_read(&sdp->sd_glock_held_count));
	gfs_printf("sd_inode_count:incore inodes::%d\n",
		   atomic_read(&sdp->sd_inode_count));
	gfs_printf("sd_bufdata_count:metadata buffers::%d\n",
		   atomic_read(&sdp->sd_bufdata_count));
	gfs_printf("sd_unlinked_ic_count:unlinked inodes::%d\n",
		   atomic_read(&sdp->sd_unlinked_ic_count));
	gfs_printf("sd_quota_count:quota IDs::%d\n",
		   atomic_read(&sdp->sd_quota_count));
	gfs_printf("sd_log_buffers:incore log buffers::%u\n",
		   sdp->sd_log_buffers);
	gfs_printf("sd_log_seg_free:log segments free::%u\n",
		   sdp->sd_log_seg_free);
	gfs_printf("ji_nsegment:log segments total::%u\n",
		   sdp->sd_jdesc.ji_nsegment);
	gfs_printf("sd_mhc_count:meta header cache entries::%d\n",
		   atomic_read(&sdp->sd_mhc_count));
	gfs_printf("sd_depend_count:glock dependencies::%d\n",
		   atomic_read(&sdp->sd_depend_count));
	gfs_printf("sd_reclaim_count:glocks on reclaim list::%d\n",
		   atomic_read(&sdp->sd_reclaim_count));
	gfs_printf("sd_log_wrap:log wraps::%"PRIu64"\n",
		   sdp->sd_log_wrap);
	gfs_printf("sd_lm_outstanding:outstanding LM calls::%d\n",
		   atomic_read(&sdp->sd_lm_outstanding));
	gfs_printf("sd_bio_outstanding:outstanding BIO calls::%u\n",
		   atomic_read(&sdp->sd_bio_outstanding));
	gfs_printf("sd_fh2dentry_misses:fh2dentry misses:diff:%u\n",
		   handle_roll(&sdp->sd_fh2dentry_misses));
	gfs_printf("sd_reclaimed:glocks reclaimed:diff:%u\n",
		   handle_roll(&sdp->sd_reclaimed));
	gfs_printf("sd_glock_nq_calls:glock nq calls:diff:%u\n",
		   handle_roll(&sdp->sd_glock_nq_calls));
	gfs_printf("sd_glock_dq_calls:glock dq calls:diff:%u\n",
		   handle_roll(&sdp->sd_glock_dq_calls));
	gfs_printf("sd_glock_prefetch_calls:glock prefetch calls:diff:%u\n",
		   handle_roll(&sdp->sd_glock_prefetch_calls));
	gfs_printf("sd_lm_lock_calls:lm_lock calls:diff:%u\n",
		   handle_roll(&sdp->sd_lm_lock_calls));
	gfs_printf("sd_lm_unlock_calls:lm_unlock calls:diff:%u\n",
		   handle_roll(&sdp->sd_lm_unlock_calls));
	gfs_printf("sd_lm_callbacks:lm callbacks:diff:%u\n",
		   handle_roll(&sdp->sd_lm_callbacks));
	gfs_printf("sd_ops_address:address operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_address));
	gfs_printf("sd_ops_dentry:dentry operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_dentry));
	gfs_printf("sd_ops_export:export operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_export));
	gfs_printf("sd_ops_file:file operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_file));
	gfs_printf("sd_ops_inode:inode operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_inode));
	gfs_printf("sd_ops_super:super operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_super));
	gfs_printf("sd_ops_vm:vm operations:diff:%u\n",
		   handle_roll(&sdp->sd_ops_vm));
	gfs_printf("sd_bio_reads:block I/O reads:diff:%u\n",
		   handle_roll(&sdp->sd_bio_reads));
	gfs_printf("sd_bio_writes:block I/O writes:diff:%u\n",
		   handle_roll(&sdp->sd_bio_writes));

        error = 0;

 out:
        RETURN(GFN_GI_GET_COUNTERS, error);
}

/**
 * gi_get_tune - Return current values of the tuneable parameters
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_get_tune(struct gfs_inode *ip,
	    struct gfs_ioctl *gi,
	    char *buf,
	    unsigned int size,
	    unsigned int *count)
{
 	ENTER(GFN_GI_GET_TUNE)
	struct gfs_tune *gt = &ip->i_sbd->sd_tune;
        int error = -ENOBUFS;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_TUNE, -EINVAL);

	spin_lock(&gt->gt_spin);

	gfs_printf("version 0\n");
        gfs_printf("ilimit1 %u\n", gt->gt_ilimit1);
        gfs_printf("ilimit1_tries %u\n", gt->gt_ilimit1_tries);
        gfs_printf("ilimit1_min %u\n", gt->gt_ilimit1_min);
        gfs_printf("ilimit2 %u\n", gt->gt_ilimit2);
        gfs_printf("ilimit2_tries %u\n", gt->gt_ilimit2_tries);
        gfs_printf("ilimit2_min %u\n", gt->gt_ilimit2_min);
        gfs_printf("demote_secs %u\n", gt->gt_demote_secs);
        gfs_printf("incore_log_blocks %u\n", gt->gt_incore_log_blocks);
        gfs_printf("jindex_refresh_secs %u\n", gt->gt_jindex_refresh_secs);
        gfs_printf("depend_secs %u\n", gt->gt_depend_secs);
        gfs_printf("scand_secs %u\n", gt->gt_scand_secs);
        gfs_printf("recoverd_secs %u\n", gt->gt_recoverd_secs);
        gfs_printf("logd_secs %u\n", gt->gt_logd_secs);
        gfs_printf("quotad_secs %u\n", gt->gt_quotad_secs);
        gfs_printf("inoded_secs %u\n", gt->gt_inoded_secs);
        gfs_printf("quota_simul_sync %u\n", gt->gt_quota_simul_sync);
        gfs_printf("quota_warn_period %u\n", gt->gt_quota_warn_period);
        gfs_printf("atime_quantum %u\n", gt->gt_atime_quantum);
        gfs_printf("quota_quantum %u\n", gt->gt_quota_quantum);
        gfs_printf("quota_scale_num %u\n", gt->gt_quota_scale_num);
        gfs_printf("quota_scale_den %u\n", gt->gt_quota_scale_den);
        gfs_printf("quota_enforce %u\n", gt->gt_quota_enforce);
        gfs_printf("quota_account %u\n", gt->gt_quota_account);
        gfs_printf("new_files_jdata %u\n", gt->gt_new_files_jdata);
        gfs_printf("new_files_directio %u\n", gt->gt_new_files_directio);
        gfs_printf("max_atomic_write %u\n", gt->gt_max_atomic_write);
        gfs_printf("max_readahead %u\n", gt->gt_max_readahead);
        gfs_printf("lockdump_size %u\n", gt->gt_lockdump_size);
        gfs_printf("stall_secs %u\n", gt->gt_stall_secs);
        gfs_printf("complain_secs %u\n", gt->gt_complain_secs);
        gfs_printf("reclaim_limit %u\n", gt->gt_reclaim_limit);
        gfs_printf("entries_per_readdir %u\n", gt->gt_entries_per_readdir);
        gfs_printf("prefetch_secs %u\n", gt->gt_prefetch_secs);
        gfs_printf("statfs_slots %u\n", gt->gt_statfs_slots);
        gfs_printf("max_mhc %u\n", gt->gt_max_mhc);
        gfs_printf("greedy_default %u\n", gt->gt_greedy_default);
        gfs_printf("greedy_quantum %u\n", gt->gt_greedy_quantum);
        gfs_printf("greedy_max %u\n", gt->gt_greedy_max);

        error = 0;

 out:
	spin_unlock(&gt->gt_spin);

        RETURN(GFN_GI_GET_TUNE, error);
}

#define tune_set(f, v) \
do { \
	spin_lock(&gt->gt_spin); \
	gt->f = (v); \
	spin_unlock(&gt->gt_spin); \
} while (0)

/**
 * gi_set_tune - Set a tuneable parameter
 * @sdp:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_set_tune(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_SET_TUNE)
	struct gfs_tune *gt = &sdp->sd_tune;
 	char param[ARG_SIZE], value[ARG_SIZE];
	unsigned int x;

	if (!capable(CAP_SYS_ADMIN))
                RETURN(GFN_GI_SET_TUNE, -EACCES);
	if (gi->gi_argc != 3)
		RETURN(GFN_GI_SET_TUNE, -EINVAL);

	if (strncpy_from_user(param, gi->gi_argv[1], ARG_SIZE) < 0)
		RETURN(GFN_GI_SET_TUNE, -EFAULT);
	param[ARG_SIZE - 1] = 0;

	if (strncpy_from_user(value, gi->gi_argv[2], ARG_SIZE) < 0)
		RETURN(GFN_GI_SET_TUNE, -EFAULT);
	value[ARG_SIZE - 1] = 0;

	if (strcmp(param, "ilimit1") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_ilimit1, x);

	} else if (strcmp(param, "ilimit1_tries") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_ilimit1_tries, x);

	} else if (strcmp(param, "ilimit1_min") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_ilimit1_min, x);

	} else if (strcmp(param, "ilimit2") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_ilimit2, x);

	} else if (strcmp(param, "ilimit2_tries") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_ilimit2_tries, x);

	} else if (strcmp(param, "ilimit2_min") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_ilimit2_min, x);

	} else if (strcmp(param, "demote_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_demote_secs, x);

	} else if (strcmp(param, "incore_log_blocks") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_incore_log_blocks, x);

	} else if (strcmp(param, "jindex_refresh_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_jindex_refresh_secs, x);

	} else if (strcmp(param, "depend_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_depend_secs, x);

	} else if (strcmp(param, "scand_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_scand_secs, x);
		wake_up_process(sdp->sd_scand_process);

	} else if (strcmp(param, "recoverd_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_recoverd_secs, x);
		wake_up_process(sdp->sd_recoverd_process);

	} else if (strcmp(param, "logd_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_logd_secs, x);
		wake_up_process(sdp->sd_logd_process);

	} else if (strcmp(param, "quotad_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_quotad_secs, x);
		wake_up_process(sdp->sd_quotad_process);

	} else if (strcmp(param, "inoded_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_inoded_secs, x);
		wake_up_process(sdp->sd_inoded_process);

	} else if (strcmp(param, "quota_simul_sync") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_quota_simul_sync, x);

	} else if (strcmp(param, "quota_warn_period") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_quota_warn_period, x);

	} else if (strcmp(param, "atime_quantum") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_atime_quantum, x);

	} else if (strcmp(param, "quota_quantum") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_quota_quantum, x);

	} else if (strcmp(param, "quota_scale") == 0) {
		unsigned int y;
		if (sscanf(value, "%u %u", &x, &y) != 2 || !y)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		spin_lock(&gt->gt_spin);
		gt->gt_quota_scale_num = x;
		gt->gt_quota_scale_den = y;
		spin_unlock(&gt->gt_spin);

	} else if (strcmp(param, "quota_enforce") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		x = !!x;
		spin_lock(&gt->gt_spin);
		gt->gt_quota_enforce = x;
		if (x)
			gt->gt_quota_account = 1;
		spin_unlock(&gt->gt_spin);

	} else if (strcmp(param, "quota_account") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		x = !!x;
		spin_lock(&gt->gt_spin);
		gt->gt_quota_account = x;
		if (x)
			spin_unlock(&gt->gt_spin);
		else {
			unsigned int y;
			gt->gt_quota_enforce = 0;
			spin_unlock(&gt->gt_spin);
			for (y = 0; y < 2; y++) {
				gfs_log_flush(sdp);
				gfs_sync_meta(sdp);
				gfs_quota_sync(sdp);
			}
		}

	} else if (strcmp(param, "new_files_jdata") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		x = !!x;
		tune_set(gt_new_files_jdata, x);

	} else if (strcmp(param, "new_files_directio") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		x = !!x;
		tune_set(gt_new_files_directio, x);

	} else if (strcmp(param, "max_atomic_write") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_max_atomic_write, x);

	} else if (strcmp(param, "max_readahead") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_max_readahead, x);

	} else if (strcmp(param, "lockdump_size") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_lockdump_size, x);

	} else if (strcmp(param, "stall_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_stall_secs, x);

	} else if (strcmp(param, "complain_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_complain_secs, x);

	} else if (strcmp(param, "reclaim_limit") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_reclaim_limit, x);

	} else if (strcmp(param, "entries_per_readdir") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_entries_per_readdir, x);

	} else if (strcmp(param, "prefetch_secs") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_prefetch_secs, x);

	} else if (strcmp(param, "statfs_slots") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_statfs_slots, x);

	} else if (strcmp(param, "max_mhc") == 0) {
		if (sscanf(value, "%u", &x) != 1)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_max_mhc, x);

	} else if (strcmp(param, "greedy_default") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_greedy_default, x);

	} else if (strcmp(param, "greedy_quantum") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_greedy_quantum, x);

	} else if (strcmp(param, "greedy_max") == 0) {
		if (sscanf(value, "%u", &x) != 1 || !x)
			RETURN(GFN_GI_SET_TUNE, -EINVAL);
		tune_set(gt_greedy_max, x);

	} else
		RETURN(GFN_GI_SET_TUNE, -EINVAL);

	RETURN(GFN_GI_SET_TUNE, 0);
}

/**
 * gi_do_reclaim - Reclaim unused metadata
 * @ip:
 * @gi:
 * @buf:
 * @size:
 * @count:
 *
 * Returns: errno
 */ 

static int
gi_do_reclaim(struct gfs_inode *ip,
	      struct gfs_ioctl *gi,
	      char *buf,
	      unsigned int size,
	      unsigned int *count)
{
	ENTER(GFN_GI_DO_RECLAIM)
	uint64_t inodes, metadata;
        int error;

	if (!capable(CAP_SYS_ADMIN))
		RETURN(GFN_GI_DO_RECLAIM, -EACCES);
	if (gi->gi_argc != 1)
		RETURN(GFN_GI_DO_RECLAIM, -EINVAL);

	error = gfs_reclaim_metadata(ip->i_sbd,
				     &inodes,
				     &metadata);
	if (error)
		RETURN(GFN_GI_DO_RECLAIM, error);

	error = -ENOBUFS;

	gfs_printf("version 0\n");
        gfs_printf("inodes %"PRIu64"\n", inodes);
        gfs_printf("metadata %"PRIu64"\n", metadata);

        error = 0;

 out:
        RETURN(GFN_GI_DO_RECLAIM, error);
}

/**
 * gi_do_shrink - throw out unused glocks
 * @sdp:
 * @gi:
 *
 * Returns: 0
 */

static int
gi_do_shrink(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_SHRINK)
	if (!capable(CAP_SYS_ADMIN))
		RETURN(GFN_GI_DO_SHRINK, -EACCES);
	if (gi->gi_argc != 1)
		RETURN(GFN_GI_DO_SHRINK, -EINVAL);
	gfs_gl_hash_clear(sdp, FALSE);
	RETURN(GFN_GI_DO_SHRINK, 0);
}

/**
 * gi_get_file_stat -
 * @ip:
 * @gi:
 *
 * Returns: the number of bytes copied, or -errno
 */

static int
gi_get_file_stat(struct gfs_inode *ip, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_GET_FILE_STAT)
	struct gfs_holder i_gh;
	struct gfs_dinode *di;
	int error;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_FILE_STAT, -EINVAL);
	if (gi->gi_size != sizeof(struct gfs_dinode))
		RETURN(GFN_GI_GET_FILE_STAT, -EINVAL);

	di = kmalloc(sizeof(struct gfs_dinode), GFP_KERNEL);
	if (!di)
		RETURN(GFN_GI_GET_FILE_STAT, -ENOMEM);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		goto out;
	memcpy(di, &ip->i_di, sizeof(struct gfs_dinode));
	gfs_glock_dq_uninit(&i_gh);

	if (copy_to_user(gi->gi_data, di,
			 sizeof(struct gfs_dinode)))
		error = -EFAULT;
	else
		error = sizeof(struct gfs_dinode);

 out:
	kfree(di);

	RETURN(GFN_GI_GET_FILE_STAT, error);
}

/**
 * gi_set_file_flag - set or clear a flag on a file
 * @ip:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_set_file_flag(struct gfs_inode *ip, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_SET_FILE_FLAG)
	char buf[ARG_SIZE];
	int set;
	uint32_t flag;
	struct gfs_holder i_gh;
	struct buffer_head *dibh;
	int error;

	if (gi->gi_argc != 3)
		RETURN(GFN_GI_SET_FILE_FLAG, -EINVAL);

	if (strncpy_from_user(buf, gi->gi_argv[1], ARG_SIZE) < 0)
		RETURN(GFN_GI_SET_FILE_FLAG, -EFAULT);
	buf[ARG_SIZE - 1] = 0;

	if (strcmp(buf, "set") == 0)
		set = TRUE;
	else if (strcmp(buf, "clear") == 0)
		set = FALSE;
	else
		RETURN(GFN_GI_SET_FILE_FLAG, -EINVAL);

        if (strncpy_from_user(buf, gi->gi_argv[2], ARG_SIZE) < 0)
                RETURN(GFN_GI_SET_FILE_FLAG, -EFAULT);
        buf[ARG_SIZE - 1] = 0;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		RETURN(GFN_GI_SET_FILE_FLAG, error);

	error = -EACCES;
	if (ip->i_di.di_uid != current->fsuid && !capable(CAP_FOWNER))
		goto out;

	error = -EINVAL;

	if (strcmp(buf, "jdata") == 0) {
		if (ip->i_di.di_type != GFS_FILE_REG ||
		    ip->i_di.di_size)
			goto out;
		flag = GFS_DIF_JDATA;
	} else if (strcmp(buf, "directio") == 0) {
		if (ip->i_di.di_type != GFS_FILE_REG)
			goto out;
		flag = GFS_DIF_DIRECTIO;
	} else if (strcmp(buf, "immutable") == 0) {
		/* The IMMUTABLE flag can only be changed by
		   the relevant capability. */
		error = -EPERM;
		if (!capable(CAP_LINUX_IMMUTABLE))
			goto out;
		flag = GFS_DIF_IMMUTABLE;
	} else if (strcmp(buf, "appendonly") == 0) {
                /* The APPENDONLY flag can only be changed by
                   the relevant capability. */
                error = -EPERM;
                if (!capable(CAP_LINUX_IMMUTABLE))
                        goto out;
		flag = GFS_DIF_APPENDONLY;
	} else if (strcmp(buf, "inherit_jdata") == 0) {
		if (ip->i_di.di_type != GFS_FILE_DIR) 
			goto out;
		flag = GFS_DIF_INHERIT_JDATA;
	} else if (strcmp(buf, "inherit_directio") == 0) {
		if (ip->i_di.di_type != GFS_FILE_DIR)
			goto out;
                flag = GFS_DIF_INHERIT_DIRECTIO;
	} else
		goto out;

	error = gfs_trans_begin(ip->i_sbd, 1, 0);
	if (error)
		goto out;

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		goto out_trans_end;

	if (set)
		ip->i_di.di_flags |= flag;
	else
		ip->i_di.di_flags &= ~flag;

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, dibh->b_data);

	brelse(dibh);

 out_trans_end:
	gfs_trans_end(ip->i_sbd);

 out:
	gfs_glock_dq_uninit(&i_gh);

	RETURN(GFN_GI_SET_FILE_FLAG, error);

}

/**
 * gi_get_file_meta - Return all the metadata for a file
 * @ip:
 * @gi:
 *
 * Returns: the number of bytes copied, or -errno
 */

static int
gi_get_file_meta(struct gfs_inode *ip, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_GET_FILE_META)
	struct gfs_holder i_gh;
	struct gfs_user_buffer ub;
	int error;

	if (gi->gi_argc != 1)
		RETURN(GFN_GI_GET_FILE_META, -EINVAL);

	ub.ub_data = gi->gi_data;
	ub.ub_size = gi->gi_size;
	ub.ub_count = 0;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		RETURN(GFN_GI_GET_FILE_META, error);

        error = -EACCES;
        if (ip->i_di.di_uid != current->fsuid && !capable(CAP_FOWNER))
                goto out;

	error = gfs_get_file_meta(ip, &ub);
	if (error)
		goto out;

	if (ip->i_di.di_type == GFS_FILE_DIR &&
	    (ip->i_di.di_flags & GFS_DIF_EXHASH)) {
		error = gfs_get_dir_meta(ip, &ub);
		if (error)
			goto out;
	}

	if (ip->i_di.di_eattr) {
		error = gfs_get_eattr_meta(ip, &ub);
		if (error)
			goto out;
	}

	error = ub.ub_count;

 out:
	gfs_glock_dq_uninit(&i_gh);

	RETURN(GFN_GI_GET_FILE_META, error);
}

/**
 * gi_do_file_flush - sync out all dirty data and
 *                    drop the cache (and lock) for a file.
 * @ip:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_do_file_flush(struct gfs_inode *ip, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_FILE_FLUSH)
	if (gi->gi_argc != 1)
		RETURN(GFN_GI_DO_FILE_FLUSH, -EINVAL);
	gfs_glock_force_drop(ip->i_gl);
	RETURN(GFN_GI_DO_FILE_FLUSH, 0);
}

/**
 * gi2hip - return the "struct gfs_inode" for a hidden file
 * @sdp:
 * @gi:
 *
 * Returns: the "struct gfs_inode"
 */

static struct gfs_inode *
gi2hip(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI2HIP)
	char buf[ARG_SIZE];

	if (gi->gi_argc != 2)
		RETURN(GFN_GI2HIP, ERR_PTR(-EINVAL));

        if (strncpy_from_user(buf, gi->gi_argv[1], ARG_SIZE) < 0)
                RETURN(GFN_GI2HIP, ERR_PTR(-EFAULT));
        buf[ARG_SIZE - 1] = 0;

	if (strcmp(buf, "jindex") == 0)
		RETURN(GFN_GI2HIP, sdp->sd_jiinode);
	else if (strcmp(buf, "rindex") == 0)
		RETURN(GFN_GI2HIP, sdp->sd_riinode);
	else if (strcmp(buf, "quota") == 0)
		RETURN(GFN_GI2HIP, sdp->sd_qinode);
	else if (strcmp(buf, "license") == 0)
		RETURN(GFN_GI2HIP, sdp->sd_linode);
	else
		RETURN(GFN_GI2HIP, ERR_PTR(-EINVAL));
}

/**
 * gi_get_hfile_stat - get stat info on a hidden file
 * @sdp:
 * @gi:
 *
 * Returns: the number of bytes copied, or -errno
 */

static int
gi_get_hfile_stat(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_GET_HFILE_STAT)
	struct gfs_inode *ip;
	struct gfs_dinode *di;
	struct gfs_holder i_gh;
	int error;

	ip = gi2hip(sdp, gi);
	if (IS_ERR(ip))
		RETURN(GFN_GI_GET_HFILE_STAT, PTR_ERR(ip));

	if (gi->gi_size != sizeof(struct gfs_dinode))
		RETURN(GFN_GI_GET_HFILE_STAT, -EINVAL);

        di = kmalloc(sizeof(struct gfs_dinode), GFP_KERNEL);
        if (!di)
                RETURN(GFN_GI_GET_HFILE_STAT, -ENOMEM);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		goto out;
	memcpy(di, &ip->i_di, sizeof(struct gfs_dinode));
	gfs_glock_dq_uninit(&i_gh);

        if (copy_to_user(gi->gi_data, di,
                         sizeof(struct gfs_dinode)))
                error = -EFAULT;
        else
                error = sizeof(struct gfs_dinode);

 out:
	kfree(di);

	RETURN(GFN_GI_GET_HFILE_STAT, error);
}

/**
 * gi_do_hfile_read - Read data from a hidden file
 * @sdp:
 * @gi:
 *
 * Returns: the number of bytes read, or -errno
 */

static int
gi_do_hfile_read(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_HFILE_READ)
	struct gfs_inode *ip;
	struct gfs_holder i_gh;
	int error;

        if (!capable(CAP_SYS_ADMIN))
                RETURN(GFN_GI_DO_HFILE_READ, -EACCES);

	ip = gi2hip(sdp, gi);
	if (IS_ERR(ip))
		RETURN(GFN_GI_DO_HFILE_READ, PTR_ERR(ip));

	if (!access_ok(VERIFY_WRITE, gi->gi_data, gi->gi_size))
		RETURN(GFN_GI_DO_HFILE_READ, -EFAULT);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &i_gh);
	if (error)
		RETURN(GFN_GI_DO_HFILE_READ, error);

	error = gfs_readi(ip, gi->gi_data, gi->gi_offset, gi->gi_size,
			  gfs_copy2user);

	gfs_glock_dq_uninit(&i_gh);

	RETURN(GFN_GI_DO_HFILE_READ, error);
}

/**
 * gi_do_hfile_write - Write data to a hidden file
 * @sdp:
 * @gi:
 *
 * Returns: the number of bytes written, or -errno
 */

static int
gi_do_hfile_write(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_HFILE_WRITE)
	struct gfs_inode *ip;
	struct gfs_alloc *al = NULL;
	struct gfs_holder i_gh;
	unsigned int data_blocks, ind_blocks;
	int alloc_required;
	int error;

        if (!capable(CAP_SYS_ADMIN))
                RETURN(GFN_GI_DO_HFILE_WRITE, -EACCES);

	ip = gi2hip(sdp, gi);
	if (IS_ERR(ip))
		RETURN(GFN_GI_DO_HFILE_WRITE, PTR_ERR(ip));

	if (!access_ok(VERIFY_READ, gi->gi_data, gi->gi_size))
		RETURN(GFN_GI_DO_HFILE_WRITE, -EFAULT);

	gfs_write_calc_reserv(ip, gi->gi_size, &data_blocks, &ind_blocks);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE,
				  LM_FLAG_PRIORITY | GL_SYNC, &i_gh);
	if (error)
		RETURN(GFN_GI_DO_HFILE_WRITE, error);

        if (!gfs_is_jdata(ip)) {
                gfs_consist_inode(ip);
		error = -EIO;
		goto out;
        }

	error = gfs_write_alloc_required(ip, gi->gi_offset, gi->gi_size,
					 &alloc_required);
	if (error)
		goto out;

	if (alloc_required) {
		al = gfs_alloc_get(ip);

		error = gfs_quota_hold_m(ip, NO_QUOTA_CHANGE,
					 NO_QUOTA_CHANGE);
		if (error)
			goto out_alloc;

		al->al_requested_meta = ind_blocks + data_blocks;

		error = gfs_inplace_reserve(ip);
		if (error)
			goto out_qs;

		/* Trans may require:
		   All blocks for a RG bitmap, all the "data" blocks, whatever
		   indirect blocks we need, a modified dinode, and a quota change */

		error = gfs_trans_begin(sdp,
					1 + al->al_rgd->rd_ri.ri_length +
					ind_blocks + data_blocks, 1);
		if (error)
			goto out_relse;
	} else {
		/* Trans may require:
		   All the "data" blocks and a modified dinode. */

		error = gfs_trans_begin(sdp, 1 + data_blocks, 0);
		if (error)
			goto out_relse;
	}

	error = gfs_writei(ip, gi->gi_data, gi->gi_offset, gi->gi_size,
			   gfs_copy_from_user);

	gfs_trans_end(sdp);

 out_relse:
	if (alloc_required) {
		gfs_assert_warn(sdp, error || al->al_alloced_meta);
		gfs_inplace_release(ip);
	}

 out_qs:
	if (alloc_required)
		gfs_quota_unhold_m(ip);

 out_alloc:
	if (alloc_required)
		gfs_alloc_put(ip);

 out:
	ip->i_gl->gl_vn++;
	gfs_glock_dq_uninit(&i_gh);

	RETURN(GFN_GI_DO_HFILE_WRITE, error);
}

/**
 * gi_do_hfile_trunc - truncate a hidden file
 * @sdp:
 * @gi:
 *
 * Returns: the number of bytes copied, or -errno
 */

static int
gi_do_hfile_trunc(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_HFILE_TRUNC)
	struct gfs_inode *ip;
	struct gfs_holder i_gh;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		RETURN(GFN_GI_DO_HFILE_TRUNC, -EACCES);

	ip = gi2hip(sdp, gi);
	if (IS_ERR(ip))
		RETURN(GFN_GI_DO_HFILE_TRUNC, PTR_ERR(ip));

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_SYNC, &i_gh);
	if (error)
		RETURN(GFN_GI_DO_HFILE_TRUNC, error);

	error = gfs_truncatei(ip, gi->gi_offset, NULL);

	ip->i_gl->gl_vn++;
	gfs_glock_dq_uninit(&i_gh);

	RETURN(GFN_GI_DO_HFILE_TRUNC, error);
}

/**
 * gi_do_quota_sync - sync the outstanding quota changes for a FS
 * @sdp:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_do_quota_sync(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_QUOTA_SYNC)
	if (!capable(CAP_SYS_ADMIN))
		RETURN(GFN_GI_DO_QUOTA_SYNC, -EACCES);
	if (gi->gi_argc != 1)
		RETURN(GFN_GI_DO_QUOTA_SYNC, -EINVAL);
	RETURN(GFN_GI_DO_QUOTA_SYNC, gfs_quota_sync(sdp));
}

/**
 * gi_do_quota_refresh - Refresh the a quota LVB from the quota file
 * @sdp:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_do_quota_refresh(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_QUOTA_REFRESH)
	char buf[ARG_SIZE];
	int user;
	uint32_t id;

	if (!capable(CAP_SYS_ADMIN))
		RETURN(GFN_GI_DO_QUOTA_REFRESH, -EACCES);
	if (gi->gi_argc != 2)
		RETURN(GFN_GI_DO_QUOTA_REFRESH, -EINVAL);

        if (strncpy_from_user(buf, gi->gi_argv[1], ARG_SIZE) < 0)
                RETURN(GFN_GI_DO_QUOTA_REFRESH, -EFAULT);
        buf[ARG_SIZE - 1] = 0;

	switch (buf[0]) {
	case 'u':
		user = TRUE;
		break;
	case 'g':
		user = FALSE;
		break;
	default:
		RETURN(GFN_GI_DO_QUOTA_REFRESH, -EINVAL);
	}

	if (buf[1] != ':')
		RETURN(GFN_GI_DO_QUOTA_REFRESH, -EINVAL);

	if (sscanf(buf + 2, "%u", &id) != 1)
		RETURN(GFN_GI_DO_QUOTA_REFRESH, -EINVAL);

	RETURN(GFN_GI_DO_QUOTA_REFRESH,
	       gfs_quota_refresh(sdp, user, id));
}

/**
 * gi_do_quota_read - read quota values from the quota file
 * @sdp:
 * @gi:
 *
 * Returns: errno
 */

static int
gi_do_quota_read(struct gfs_sbd *sdp, struct gfs_ioctl *gi)
{
	ENTER(GFN_GI_DO_QUOTA_READ)
	char buf[ARG_SIZE];
	int user;
	uint32_t id;
	struct gfs_quota q;
	int error;

	if (gi->gi_argc != 2)
		RETURN(GFN_GI_DO_QUOTA_READ, -EINVAL);
	if (gi->gi_size != sizeof(struct gfs_quota))
		RETURN(GFN_GI_DO_QUOTA_READ, -EINVAL);

        if (strncpy_from_user(buf, gi->gi_argv[1], ARG_SIZE) < 0)
                RETURN(GFN_GI_DO_QUOTA_READ, -EFAULT);
        buf[ARG_SIZE - 1] = 0;

	switch (buf[0]) {
	case 'u':
		user = TRUE;
		break;
	case 'g':
		user = FALSE;
		break;
	default:
		RETURN(GFN_GI_DO_QUOTA_READ, -EINVAL);
	}

	if (buf[1] != ':')
		RETURN(GFN_GI_DO_QUOTA_READ, -EINVAL);

	if (sscanf(buf + 2, "%u", &id) != 1)
		RETURN(GFN_GI_DO_QUOTA_READ, -EINVAL);

	error = gfs_quota_read(sdp, user, id, &q);
	if (error)
		RETURN(GFN_GI_DO_QUOTA_READ, error);

	if (copy_to_user(gi->gi_data, &q, sizeof(struct gfs_quota)))
		RETURN(GFN_GI_DO_QUOTA_READ, -EFAULT);

	RETURN(GFN_GI_DO_QUOTA_READ, 0);
}

/**
 * gfs_ioctl_i -
 * @ip:
 * @arg:
 *
 * Returns: -errno or positive byte count
 */

int
gfs_ioctl_i(struct gfs_inode *ip, void *arg)
{
	ENTER(GFN_IOCTL_I)
	struct gfs_ioctl *gi_user = (struct gfs_ioctl *)arg;
	struct gfs_ioctl gi;
	char **argv;
	char arg0[ARG_SIZE];
	int error = -EFAULT;

	if (copy_from_user(&gi, gi_user, sizeof(struct gfs_ioctl)))
		RETURN(GFN_IOCTL_I, -EFAULT);
	if (!gi.gi_argc)
		RETURN(GFN_IOCTL_I, -EINVAL);
	argv = kmalloc(gi.gi_argc * sizeof(char *), GFP_KERNEL);
	if (!argv)
		RETURN(GFN_IOCTL_I, -ENOMEM);
	if (copy_from_user(argv, gi.gi_argv,
			   gi.gi_argc * sizeof(char *)))
		goto out;
	gi.gi_argv = argv;

	if (strncpy_from_user(arg0, argv[0], ARG_SIZE) < 0)
		goto out;
	arg0[ARG_SIZE - 1] = 0;

	if (strcmp(arg0, "get_cookie") == 0)
                error = gi_skeleton(ip, &gi, gi_get_cookie);
	else if (strcmp(arg0, "get_super") == 0)
		error = gi_get_super(ip->i_sbd, &gi);
	else if (strcmp(arg0, "get_args") == 0)
		error = gi_skeleton(ip, &gi, gi_get_args);
	else if (strcmp(arg0, "get_lockstruct") == 0)
		error = gi_skeleton(ip, &gi, gi_get_lockstruct);
        else if (strcmp(arg0, "get_stat_gfs") == 0)
                error = gi_skeleton(ip, &gi, gi_get_stat_gfs);
        else if (strcmp(arg0, "get_counters") == 0)
                error = gi_skeleton(ip, &gi, gi_get_counters);
        else if (strcmp(arg0, "get_tune") == 0)
                error = gi_skeleton(ip, &gi, gi_get_tune);
	else if (strcmp(arg0, "set_tune") == 0)
		error = gi_set_tune(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_reclaim") == 0)
		error = gi_skeleton(ip, &gi, gi_do_reclaim);
	else if (strcmp(arg0, "do_shrink") == 0)
		error = gi_do_shrink(ip->i_sbd, &gi);
	else if (strcmp(arg0, "get_file_stat") == 0)
		error = gi_get_file_stat(ip, &gi);
	else if (strcmp(arg0, "set_file_flag") == 0)
		error = gi_set_file_flag(ip, &gi);
	else if (strcmp(arg0, "get_file_meta") == 0)
		error = gi_get_file_meta(ip, &gi);
	else if (strcmp(arg0, "do_file_flush") == 0)
		error = gi_do_file_flush(ip, &gi);
	else if (strcmp(arg0, "get_hfile_stat") == 0)
		error = gi_get_hfile_stat(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_hfile_read") == 0)
		error = gi_do_hfile_read(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_hfile_write") == 0)
		error = gi_do_hfile_write(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_hfile_trunc") == 0)
		error = gi_do_hfile_trunc(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_quota_sync") == 0)
		error = gi_do_quota_sync(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_quota_refresh") == 0)
		error = gi_do_quota_refresh(ip->i_sbd, &gi);
	else if (strcmp(arg0, "do_quota_read") == 0)
		error = gi_do_quota_read(ip->i_sbd, &gi);
	else
		error = -ENOTTY;

 out:
	kfree(argv);

	RETURN(GFN_IOCTL_I, error);
}


