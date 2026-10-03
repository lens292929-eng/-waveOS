typedef struct {
    u64 framebuffer;
    u32 width;
    u32 height;
    u32 pitch;
    u32 pad;
    u64 memory_map;
    usize memory_map_size;
    usize memory_descriptor_size;
} BootInfo;