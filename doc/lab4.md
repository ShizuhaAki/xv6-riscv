# Lab 4 xv6 文件系统扩展实验报告

**姓名**: 朱程炀 (Zecyel)
**学号**: 23300240014
**日期**: 2026.1.10

## 实验概述

本实验实现了 xv6-riscv 的四个文件系统扩展：Large Files（双重间接块）、Symbolic Links（符号链接）、Long Filenames（长文件名）、Hard Links & unlink 语义。所有修改确保磁盘一致性，使用日志持久化元数据变化（如 nlink、addrs）。新增测试程序已加入 Makefile UPROGS。

**测试结果**:
```
xv6 kernel is booting
hart 1 starting
hart 2 starting
khugepaged: daemon started
init: starting sh
$ longnametest
longnametest: ok
$ symlinktest
symlinktest: ok
$ hardlinktest
hardlinktest: ok
$ bigwrite
bigwrite: ok
$
```

## 实验 1：Large Files

### 实现细节 (kernel/fs.h, kernel/fs.c)

- **磁盘布局扩展** (`fs.h`):
  ```
  #define NDIRECT 10
  #define NINDIRECT (BSIZE / sizeof(uint))  // 256
  #define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * (uint64)NINDIRECT)  // ~67MB
  struct dinode {
    ...
    uint addrs[NDIRECT + 2];  // [0..9] direct, [10] single-indirect, [11] double-indirect
  };
  ```

- **bmap() 逻辑** (`fs.c:bmap`):
  - bn < NDIRECT: direct `ip->addrs[bn]`
  - NDIRECT <= bn < NDIRECT + NINDIRECT: single-indirect `bn -= NDIRECT`, load `ip->addrs[NDIRECT]`, `a[bn]`
  - bn >= NDIRECT + NINDIRECT: double-indirect `bn -= NDIRECT + NINDIRECT`, `pbn = bn / NINDIRECT`, `sbn = bn % NINDIRECT`
    - Load primary `ip->addrs[NDIRECT+1] -> a[pbn] -> secondary`
  - 按需分配，按序释放。

- **itrunc() 释放** (`fs.c:itrunc`):
  - Free direct [0..NDIRECT-1]
  - Single: load `addrs[NDIRECT]`, free all `a[j]`, free indirect
  - Double: load `addrs[NDIRECT+1]`, for each primary `pb = a[i]`: load `pb`, free all `a2[j]`, free `pb`; free double-indirect

- **mkfs 兼容**: `mkfs.c:iappend` 只用 direct + single（小文件），无需改。

**验证**: `bigwrite` 写 3000 × 1024B (~3MB) 落 double-indirect，ok。`itrunc` 释放间接块（bfree），无泄漏。

## 实验 2：Symbolic Links

### 实现细节 (kernel/stat.h, kernel/fcntl.h, kernel/sysfile.c, kernel/syscall.*)

- **inode 类型** (`stat.h`): `#define T_SYMLINK 4`

- **O_NOFOLLOW** (`fcntl.h`): `#define O_NOFOLLOW 0x08000000`

- **sys_symlink** (`sysfile.c:sys_symlink`, SYS=26):
  - `create(path, T_SYMLINK, 0, 0)` 新 inode
  - `writei(ip, 0, target, 0, strlen(target)+1)` 存 target + '\0'
  - 允许 dangling（target 不存在）

- **sys_open 跟随** (`sysfile.c:sys_open`):
  ```
  ilock(ip);
  int depth = 0;
  while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW) && depth < 32) {
    char target[MAXPATH+1];
    int n = readi(ip, 0, target, 0, MAXPATH);
    iunlock(ip);
    if(n <= 0 || n >= MAXPATH) error;
    target[n] = 0;
    iput(ip);
    ip = namei(target);
    if(!ip) error;
    ilock(ip);
    depth++;
  }
  ```
  - 深度限 32 防循环
  - O_NOFOLLOW 开 symlink 本身

- **系统调用**: `syscall.h` SYS_symlink=26, `usys.pl` entry, `user.h` decl.

**验证**: `symlinktest` 覆盖 basic/dangling/loop/O_NOFOLLOW，ok。路径解析不跳 symlink 本身。

## 实验 3：Long Filenames

### 实现细节 (kernel/fs.h, kernel/fs.c, kernel/sysfile.c)

- **常量** (`fs.h`):
  ```
  #define DIRSIZ 14
  #define MAXCOMPONENTSZ 128
  #define LONGNAME_CONT 0xFFFF  // ushort max-1
  #define MAXCHAIN 20  // 280B max
  ```

- **dirlookup** (`fs.c:dirlookup`):
  - 扫描 off += sizeof(dirent)
  - inum !=0: chain_start=off, cur_off=off, flen=0, chain_len=0
  - while valid/cur_off<size/chain_len<20: chain_len++, readi cde[cur_off], if flen+14>128 valid=0
  - copy cde.name[14] to fullname+flen, flen+=14
  - if cde.inum != LONGNAME_CONT: chain_inum=cde.inum break; cur_off+=16
  - fullname[flen]=0
  - if valid && chain_inum && chain_len<20 && **strncmp(name, fullname, namelen)==0 && fullname[namelen]=='\0'** → exact match, return iget(chain_inum), *poff=chain_start

- **dirlink** (`fs.c:dirlink`):
  - if dirlookup(dp,name,0) exist → -1
  - namelen=strlen(name), 0<namelen<=128
  - nslots = (namelen +13)/14
  - Find consecutive hole_slots >=nslots (0 inum) or append off=dp->size
  - for i=0..nslots-1: nde.inum=(i last? inum : CONT), take=min(14, remain), copy name[pos:take], writei(off+i*16, nde)

- **unlink chain clear** (`sysfile.c:sys_unlink`):
  ```
  uint cur_off = off;
  while(1) {
    readi pde[cur_off];
    writei zde[cur_off];
    if(pde.inum != LONGNAME_CONT) break;
    cur_off +=16;
  }
  ```

- **skipelem fix** (`fs.c:skipelem`): copy min(path-s, MAXCOMPONENTSZ), name[clen]=0 (full component)

**验证**: `longnametest` (50+ chars) create/read/unlink ok。短/长混存，unlink 无 orphan CONT。

## 实验 4：Hard Link & unlink

### 实现细节 (kernel/sysfile.c)

- **sys_link** (`sysfile.c:sys_link`):
  ```
  ip = namei(old); ilock(ip);
  if(T_DIR) error;
  ip->nlink++; iupdate(ip); iunlock(ip);
  dp = nameiparent(new, name); ilock(dp);
  if(dev mismatch || dirlink(dp, name, ip->inum)<0) → bad: ip nlink--, iupdate
  iunlockput(dp); iput(ip);
  ```
  - nlink++ 前置，回滚失败
  - 禁 dir hardlink（树结构）

- **sys_unlink**: 删 chain，清 nlink-- iupdate(ip)（dir: dp nlink--）
- **iput/itrunc**: nlink==0 && ref==1 → itrunc + type=0
- **ireclaim**: boot 回收 orphan nlink==0 inode

**验证**: `hardlinktest` fd open + link + unlink原名 + write + read新名 ok（nlink=1, ref>0 无 trunc）。

## 常见问题修复

- dirlookup 前缀匹配 → exact `strncmp + nul check`
- skipelem 长组件截断无\0 → copy 128 + \0
- 持久化: 所有 nlink/addrs 用 iupdate(log_write)，间接块 bfree

## 提交

- Git commits: feat/large-files, feat/symlink 等
- Tests: user/bigwrite.c longnametest.c symlinktest.c hardlinktest.c (Makefile UPROGS)
- 本报告 `doc/lab4.md`

**性能**: 无退化，bigwrite ~3MB double-indirect ok。