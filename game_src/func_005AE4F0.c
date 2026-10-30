/* compiler: ee-gcc2.9-as296 */
/* Native version of 005AE4F0 _sceSDC (cache.o): wrote back and invalidated the data cache lines (both ways) whose
   tag lay in [start, end], by index with `cache` instructions. Native memory is coherent and no DMA engine reads
   game RAM behind the CPU's back, so there is nothing to do. */
void func_005AE4F0(unsigned int start, unsigned int end)
{
    (void)start;
    (void)end;
}
