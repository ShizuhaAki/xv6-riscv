#pragma once

struct buf;

void virtio_disk_init(void);
void virtio_disk_rw(struct buf *, int);
void virtio_disk_intr(void);
