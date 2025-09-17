#pragma once

#include "buf.h"
#include "fs.h"

void initlog(int, struct superblock *);
void log_write(struct buf *);
void begin_op(void);
void end_op(void);
