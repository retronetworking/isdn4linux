/*
 * $Id$
 * 
 * (c) Copyright 2000 by Carsten Paeth (calle@calle.de)
 *
 * Heavily based on devpts filesystem from H. Peter Anvin
 * 
 * $Log$
 *
 */

#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Carsten Paeth <calle@calle.de>");

static char *revision = "$Revision$";

struct capifs_ncci {
	struct inode *inode;
	u16 applid;
	u32 ncci;
	char type[4];
};

struct capifs_sb_info {
	u32 magic;
	struct super_block *next;
	struct super_block **back;
	int setuid;
	int setgid;
	uid_t   uid;
	gid_t   gid;
	umode_t mode;

	unsigned int max_ncci;
	struct capifs_ncci *nccis;
};

#define CAPIFS_SUPER_MAGIC (('C'<<8)|'N')
#define CAPIFS_SBI_MAGIC   (('C'<<24)|('A'<<16)|('P'<<8)|'I')

static inline struct capifs_sb_info *SBI(struct super_block *sb)
{
	return (struct capifs_sb_info *)(sb->u.generic_sbp);
}

/* ------------------------------------------------------------------ */

static int capifs_root_readdir(struct file *,void *,filldir_t);
static struct dentry *capifs_root_lookup(struct inode *,struct dentry *);
static int capifs_revalidate(struct dentry *, int);

static struct file_operations capifs_root_operations = {
	NULL,                   /* llseek */
	NULL,                   /* read */
	NULL,                   /* write */
	capifs_root_readdir,    /* readdir */
};

struct inode_operations capifs_root_inode_operations = {
	&capifs_root_operations, /* file operations */
	NULL,                   /* create */
	capifs_root_lookup,     /* lookup */
};

static struct dentry_operations capifs_dentry_operations = {
	capifs_revalidate,	/* d_revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
};

/*
 * The normal naming convention is simply
 * /dev/capi/tty<appl>-<ncci>
 * /dev/capi/raw<appl>-<ncci>
 */

static int capifs_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct capifs_sb_info * sbi = SBI(filp->f_dentry->d_inode->i_sb);
	off_t nr;
	char numbuf[32];

	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		while ( nr < sbi->max_ncci ) {
			int n = nr - 2;
			if ( sbi->nccis[n].inode ) {
				memcpy(numbuf, sbi->nccis[n].type, 3);
				sprintf(numbuf+3, "%u-%x",
					sbi->nccis[n].applid,
					sbi->nccis[n].ncci);
				if ( filldir(dirent, numbuf, strlen(numbuf), nr, nr) < 0 )
					return 0;
			}
			filp->f_pos = ++nr;
		}
		break;
	}

	return 0;
}

/*
 * Revalidate is called on every cache lookup.  We use it to check that
 * the ncci really does still exist.  Never revalidate negative dentries;
 * for simplicity (fix later?)
 */
static int capifs_revalidate(struct dentry * dentry, int flags)
{
	struct capifs_sb_info *sbi;

	if ( !dentry->d_inode )
		return 0;

	sbi = SBI(dentry->d_inode->i_sb);

	return ( sbi->nccis[dentry->d_inode->i_ino - 2].inode == dentry->d_inode );
}

static struct dentry *capifs_root_lookup(struct inode * dir, struct dentry * dentry)
{
	struct capifs_sb_info *sbi = SBI(dir->i_sb);
	struct capifs_ncci *np;
	unsigned int i;
	char numbuf[32];
	const char *p;
	char *tmp;
	u16 applid;
	u32 ncci;

	dentry->d_inode = NULL;	/* Assume failure */
	dentry->d_op    = &capifs_dentry_operations;

	if ( dentry->d_name.len < 6 || dentry->d_name.len >= sizeof(numbuf) )
		return NULL;
	strncpy(numbuf, dentry->d_name.name, dentry->d_name.len);
	numbuf[dentry->d_name.len] = 0;
        p = tmp = numbuf+3;
	applid = (u16)simple_strtoul(p, &tmp, 10);
	if (applid == 0 || tmp == p || *tmp != '-')
		return NULL;
	p = ++tmp;
	ncci = (u32)simple_strtoul(p, &tmp, 16);
	if (ncci == 0 || tmp == p || *tmp)
		return NULL;

	for (i = 0, np = sbi->nccis ; i < sbi->max_ncci; i++, np++) {
		if (np->applid == applid && np->ncci == ncci
			&& memcmp(numbuf, np->type, 3) == 0)
			break;
	}

	if ( i >= sbi->max_ncci )
		return NULL;

	dentry->d_inode = np->inode;
	if ( dentry->d_inode )
		dentry->d_inode->i_count++;
	
	d_add(dentry, dentry->d_inode);

	return NULL;
}

/* ------------------------------------------------------------------ */

static struct super_block *mounts = NULL;

static void capifs_put_super(struct super_block *sb)
{
	struct capifs_sb_info *sbi = SBI(sb);
	struct inode *inode;
	int i;

	for ( i = 0 ; i < sbi->max_ncci ; i++ ) {
		if ( (inode = sbi->nccis[i].inode) ) {
			if ( inode->i_count != 1 )
				printk("capifs_put_super: badness: entry %d count %d\n",
				       i, inode->i_count);
			inode->i_nlink--;
			iput(inode);
		}
	}

	*sbi->back = sbi->next;
	if ( sbi->next )
		SBI(sbi->next)->back = sbi->back;

	kfree(sbi->nccis);
	kfree(sbi);

	MOD_DEC_USE_COUNT;
}

static int capifs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static void capifs_read_inode(struct inode *inode);
static void capifs_write_inode(struct inode *inode);

static struct super_operations capifs_sops = {
	capifs_read_inode,
	capifs_write_inode,
	NULL,			/* put_inode */
	NULL,			/* delete_inode */
	NULL,			/* notify_change */
	capifs_put_super,
	NULL,			/* write_super */
	capifs_statfs,
	NULL,			/* remount_fs */
	NULL,			/* clear_inode */
};

static int capifs_parse_options(char *options, struct capifs_sb_info *sbi)
{
	int setuid = 0;
	int setgid = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	umode_t mode = 0600;
	unsigned int maxncci = 512;
	char *this_char, *value;

	this_char = NULL;
	if ( options )
		this_char = strtok(options,",");
	for ( ; this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 1;
			uid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
			setuid = 1;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 1;
			gid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
			setgid = 1;
		}
		else if (!strcmp(this_char,"mode")) {
			if (!value || !*value)
				return 1;
			mode = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"maxncci")) {
			if (!value || !*value)
				return 1;
			maxncci = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else
			return 1;
	}
	sbi->setuid   = setuid;
	sbi->setgid   = setgid;
	sbi->uid      = uid;
	sbi->gid      = gid;
	sbi->mode     = mode & ~S_IFMT;
	sbi->max_ncci = maxncci;

	return 0;
}

struct super_block *capifs_read_super(struct super_block *s, void *data,
				      int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct capifs_sb_info *sbi;

	MOD_INC_USE_COUNT;

	lock_super(s);
	/* Super block already completed? */
	if (s->s_root) {
		unlock_super(s);
		goto out;
	}

	sbi = (struct capifs_sb_info *) kmalloc(sizeof(struct capifs_sb_info), GFP_KERNEL);
	if ( !sbi ) {
		unlock_super(s);
		goto fail;
	}

	memset(sbi, 0, sizeof(struct capifs_sb_info));
	sbi->magic  = CAPIFS_SBI_MAGIC;

	/* Can this call block?  (It shouldn't) */
	if ( capifs_parse_options(data,sbi) ) {
		kfree(sbi);
		unlock_super(s);
		printk("capifs: called with bogus options\n");
		goto fail;
	}

	sbi->nccis = kmalloc(sizeof(struct capifs_ncci) * sbi->max_ncci, GFP_KERNEL);
	if ( !sbi->nccis ) {
		kfree(sbi);
		unlock_super(s);
		goto fail;
	}
	memset(sbi->nccis, 0, sizeof(struct capifs_ncci) * sbi->max_ncci);

	s->u.generic_sbp = (void *) sbi;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = CAPIFS_SUPER_MAGIC;
	s->s_op = &capifs_sops;
	s->s_root = NULL;
	unlock_super(s); /* shouldn't we keep it locked a while longer? */

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	root_inode = iget(s, 1); /* inode 1 == root directory */
#ifdef COMPAT_d_alloc_root_one_parameter
	root = d_alloc_root(root_inode);
#else
	root = d_alloc_root(root_inode, NULL);
#endif

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root) {
		if (root) dput(root);
		else iput(root_inode);
		goto out;
	}

	if (!root) {
		printk("capifs: get root dentry failed\n");
		/*
	 	* iput() can block, so we clear the super block first.
	 	*/
		s->s_dev = 0;
		iput(root_inode);
		kfree(sbi->nccis);
		kfree(sbi);
		goto fail;
	}

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root)
		goto out;
	
	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	lock_super(s);
	s->s_root = root;

	sbi->next = mounts;
	if ( sbi->next )
		SBI(sbi->next)->back = &(sbi->next);
	sbi->back = &mounts;
	mounts = s;

	unlock_super(s);
	return s;

	/*
	 * Success ... somebody else completed the super block for us. 
	 */ 
out:
	MOD_DEC_USE_COUNT;
	return s;
fail:
	s->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int capifs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = CAPIFS_SUPER_MAGIC;
	tmp.f_bsize = 1024;
	tmp.f_blocks = 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

static void capifs_read_inode(struct inode *inode)
{
	ino_t ino = inode->i_ino;
	struct capifs_sb_info *sbi = SBI(inode->i_sb);

	inode->i_op = NULL;
	inode->i_mode = 0;
	inode->i_nlink = 0;
	inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	inode->i_uid = inode->i_gid = 0;

	if ( ino == 1 ) {
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
		inode->i_op = &capifs_root_inode_operations;
		inode->i_nlink = 2;
		return;
	} 

	ino -= 2;
	if ( ino >= sbi->max_ncci )
		return;		/* Bogus */
	
#ifdef COMPAT_HAS_init_special_inode
	/* Gets filled in by capifs_new_ncci() */
	init_special_inode(inode,S_IFCHR,0);
#else
	inode->i_mode = S_IFCHR;
	inode->i_rdev = MKDEV(0,0); /* Gets filled in by devpts_pty_new() */
	inode->i_op = &chrdev_inode_operations;
#endif

	return;
}

static void capifs_write_inode(struct inode *inode)
{
}

static struct file_system_type capifs_fs_type = {
	"capifs",
	0,
	capifs_read_super,
	NULL
};

void capifs_new_ncci(__u16 applid, __u32 ncci, char *type, kdev_t device)
{
	struct super_block *sb;
	struct capifs_sb_info *sbi;
	struct capifs_ncci *np;
	char stype[4];
	char *s = type;
	ino_t ino;
	int i;

	for (i=0; i < 3; i++) {
		if (*s) 
			stype[i] = *s++;
		else
			stype[i] = '-';
	}
	stype[3] = 0;
		
	for ( sb = mounts ; sb ; sb = sbi->next ) {
		sbi = SBI(sb);

		for (ino = 0, np = sbi->nccis ; ino < sbi->max_ncci; ino++, np++) {
			if (np->applid == 0 && np->ncci == 0) {
				np->applid = applid;
				np->ncci = ncci;
				memcpy(np->type, stype, 4);
				break;
			}
		}

		if ((np->inode = iget(sb, ino+2)) != 0) {
			struct inode *inode = np->inode;
			inode->i_uid = sbi->setuid ? sbi->uid : current->fsuid;
			inode->i_gid = sbi->setgid ? sbi->gid : current->fsgid;
			inode->i_mode = sbi->mode | S_IFCHR;
			inode->i_rdev = device;
			inode->i_nlink++;
		}
	}
}

void capifs_free_ncci(__u16 applid, __u32 ncci, char *type)
{
	struct super_block *sb;
	struct capifs_sb_info *sbi;
	struct inode *inode;
	struct capifs_ncci *np;
	char stype[4];
	char *s = type;
	ino_t ino;
	int i;

	for (i=0; i < 3; i++) {
		if (*s) 
			stype[i] = *s++;
		else
			stype[i] = '-';
	}
	stype[3] = 0;

	for ( sb = mounts ; sb ; sb = sbi->next ) {
		sbi = SBI(sb);

		for (ino = 0, np = sbi->nccis ; ino < sbi->max_ncci; ino++, np++) {
			if (np->applid != applid || np->ncci != ncci)
				continue;
			if (np->inode && memcmp(stype, np->type, 4) == 0) {
				inode = np->inode;
				memset(np, 0, sizeof(struct capifs_ncci));
				inode->i_nlink--;
				iput(inode);
				break;
			}
		}
	}
}

int __init capifs_init(void)
{
	char rev[10];
	char *p;
	int err;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, "1.0");

	err = register_filesystem(&capifs_fs_type);
	if (err)
		return err;
#ifdef MODULE
        printk(KERN_NOTICE "capifs: Rev%s: loaded\n", rev);
#else
	printk(KERN_NOTICE "capifs: Rev%s: started\n", rev);
#endif
	return 0;
}

void capifs_exit(void)
{
	unregister_filesystem(&capifs_fs_type);
}

EXPORT_SYMBOL(capifs_new_ncci);
EXPORT_SYMBOL(capifs_free_ncci);

#ifdef MODULE

int init_module(void)
{
	return capifs_init();
}

void cleanup_module(void)
{
	capifs_exit();
}

#endif
